from __future__ import annotations

import argparse
import contextlib
import csv
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

import duckdb

BENCHMARK_DIR = Path(__file__).parents[1]


def load_module(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, BENCHMARK_DIR / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {filename}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


benchmark = load_module("poj_benchmark", "benchmark.py")
jplag = load_module("poj_jplag", "jplag.py")
bootstrap = load_module("poj_bootstrap", "bootstrap.py")


def write_poj_parquet(parquet_root: Path, splits: tuple[str, ...]) -> None:
    connection = duckdb.connect()
    try:
        for split in splits:
            path = (parquet_root / f"{split}-00000-of-00001.parquet").as_posix()
            connection.execute(
                f"""
                COPY (
                    SELECT * FROM (VALUES
                        (0, 'int main() {{ int a = 0; return a; }}', '1'),
                        (1, 'int main() {{ int b = 1; return b; }}', '1'),
                        (2, 'int main() {{ int c = 2; return c * 2; }}', '1'),
                        (3, 'int main() {{ for (int i = 0; i < 3; i++) {{}} return 3; }}', '2'),
                        (4, 'int main() {{ for (int j = 0; j < 4; j++) {{}} return 4; }}', '2'),
                        (5, 'int main() {{ while (true) {{ break; }} return 5; }}', '2')
                    ) AS rows(id, code, label)
                ) TO '{path}' (FORMAT PARQUET)
                """
            )
    finally:
        connection.close()


class MetricsTest(unittest.TestCase):
    def test_ranking_metrics_handle_ties(self) -> None:
        tied_scores = [(25.0, True), (25.0, False), (25.0, True), (25.0, False)]
        self.assertAlmostEqual(benchmark.roc_auc(tied_scores), 0.5)
        self.assertAlmostEqual(benchmark.average_precision(tied_scores), 0.5)

    def test_bootstrap_metrics_match_benchmark_metrics(self) -> None:
        scores = [90.0, 80.0, 70.0, 10.0, 70.0]
        labels = [True, False, True, False, False]
        pairs = list(zip(scores, labels))
        self.assertAlmostEqual(bootstrap.roc_auc(scores, labels), benchmark.roc_auc(pairs))
        self.assertAlmostEqual(
            bootstrap.average_precision(scores, labels), benchmark.average_precision(pairs)
        )

    def test_evaluate_writes_auc_and_threshold_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            results = root / "Benchmark-Result.csv"
            results.write_text("Label,Score\n1,90\n0,80\n1,70\n0,10\n", encoding="utf-8")
            output = root / "metrics.json"
            with contextlib.redirect_stdout(io.StringIO()):
                benchmark.evaluate(
                    argparse.Namespace(results=results, output=output, threshold=[50.0])
                )
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertAlmostEqual(report["roc_auc"], 0.75)
            self.assertAlmostEqual(report["pr_auc_average_precision"], 5 / 6)
            self.assertEqual(report["requested_thresholds"][0]["threshold"], 50.0)


class PojPipelineTest(unittest.TestCase):
    def test_bundle_sample_materialize_creates_balanced_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            parquet_root = root / "parquet"
            parquet_root.mkdir()
            write_poj_parquet(parquet_root, ("train", "test"))

            database = root / "poj.duckdb"
            with contextlib.redirect_stdout(io.StringIO()):
                benchmark.bundle_poj(
                    argparse.Namespace(parquet_root=parquet_root, database=database, replace=False)
                )
            connection = duckdb.connect(str(database))
            try:
                count, distinct_ids = connection.execute(
                    "SELECT count(*), count(DISTINCT submission_id) FROM submissions"
                ).fetchone()
            finally:
                connection.close()
            self.assertEqual(count, 12)
            self.assertEqual(distinct_ids, 12)

            with contextlib.redirect_stdout(io.StringIO()):
                benchmark.sample(
                    argparse.Namespace(
                        database=database, split="test",
                        positive_per_problem=2, negative_per_problem=2, seed=7,
                    )
                )
                output = root / "materialized"
                benchmark.materialize(
                    argparse.Namespace(database=database, output=output, replace=False)
                )

            with (output / "pairs.csv").open(newline="", encoding="utf-8") as stream:
                reader = csv.reader(stream)
                header = next(reader)
                rows = [dict(zip(header, row)) for row in reader]
            self.assertEqual(header, ["left_dna", "right_dna", "label", "pair_id", "kind", "split"])
            self.assertEqual(len(rows), 8)
            self.assertEqual(sum(row["label"] == "1" for row in rows), 4)
            self.assertTrue(all(row["split"] == "test" for row in rows))
            self.assertTrue(all(row["left_dna"].endswith(".cpp.DNA") for row in rows))
            referenced = {
                row[key].removesuffix(".DNA") for row in rows for key in ("left_dna", "right_dna")
            }
            materialized = {path.name for path in (output / "sources").glob("*.cpp")}
            self.assertEqual(materialized, referenced)

            # the same seed yields the same manifest
            with contextlib.redirect_stdout(io.StringIO()):
                benchmark.sample(
                    argparse.Namespace(
                        database=database, split="test",
                        positive_per_problem=2, negative_per_problem=2, seed=7,
                    )
                )
                again = root / "materialized-again"
                benchmark.materialize(
                    argparse.Namespace(database=database, output=again, replace=False)
                )
            self.assertEqual(
                (output / "pairs.csv").read_text(encoding="utf-8"),
                (again / "pairs.csv").read_text(encoding="utf-8"),
            )


class JplagMappingTest(unittest.TestCase):
    def test_chunking_covers_every_pair_within_limit(self) -> None:
        manifest = [
            {"left_dna": f"a{i}.cpp.DNA", "right_dna": f"b{i}.cpp.DNA"} for i in range(10)
        ]
        groups = jplag.chunk_manifest(manifest, max_files=4)
        self.assertTrue(all(len(group) <= 4 for group in groups))
        for row in manifest:
            left, right = row["left_dna"][:-4], row["right_dna"][:-4]
            self.assertTrue(any(left in group and right in group for group in groups))

    def test_similarities_are_keyed_by_unordered_pair(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "results.csv"
            path.write_text(
                "submissionName1,submissionName2,averageSimilarity,maxSimilarity\n"
                "x.cpp,y.cpp,0.25,0.5\n",
                encoding="utf-8",
            )
            scores = jplag.load_similarities(path)
            self.assertEqual(scores[frozenset(("y.cpp", "x.cpp"))], (25.0, 50.0))


if __name__ == "__main__":
    unittest.main()
