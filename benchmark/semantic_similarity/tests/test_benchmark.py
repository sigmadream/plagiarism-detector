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


MODULE_PATH = Path(__file__).parents[1] / "benchmark.py"
SPEC = importlib.util.spec_from_file_location("semantic_similarity_benchmark", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"could not load {MODULE_PATH}")
benchmark = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = benchmark
SPEC.loader.exec_module(benchmark)


class BenchmarkTest(unittest.TestCase):
    def test_ranking_metrics_handle_ties(self) -> None:
        tied_scores = [(25.0, True), (25.0, False), (25.0, True), (25.0, False)]

        self.assertAlmostEqual(benchmark.roc_auc(tied_scores), 0.5)
        self.assertAlmostEqual(benchmark.average_precision(tied_scores), 0.5)

    def test_codenet_pipeline_creates_balanced_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            corpus = root / "codenet"
            for problem_index in range(3):
                problem = corpus / f"p{problem_index:05d}"
                problem.mkdir(parents=True)
                for submission_index in range(3):
                    (problem / f"s{problem_index}{submission_index}.cpp").write_text(
                        f"int main() {{ return {problem_index + submission_index}; }}\n",
                        encoding="utf-8",
                    )

            database = root / "codenet.duckdb"
            benchmark.bundle_codenet(
                argparse.Namespace(source_root=corpus, database=database, replace=False)
            )
            benchmark.sample(
                argparse.Namespace(
                    database=database,
                    split=None,
                    positive_per_problem=1,
                    negative_per_problem=1,
                    seed=7,
                )
            )
            output = root / "materialized"
            benchmark.materialize(
                argparse.Namespace(database=database, output=output, replace=False)
            )

            with (output / "pairs.csv").open(newline="", encoding="utf-8") as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(len(rows), 6)
            self.assertEqual(sum(row["label"] == "1" for row in rows), 3)
            self.assertTrue(all(row["left_dna"].endswith(".cpp.DNA") for row in rows))
            referenced_sources = {
                row[key].removesuffix(".DNA")
                for row in rows
                for key in ("left_dna", "right_dna")
            }
            materialized_sources = {
                path.name for path in (output / "sources").glob("*.cpp")
            }
            self.assertEqual(materialized_sources, referenced_sources)

    def test_poj_bundle_uses_split_qualified_ids(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            parquet_root = root / "parquet"
            parquet_root.mkdir()
            connection = duckdb.connect()
            try:
                for split in ("train", "test"):
                    path = (parquet_root / f"{split}-00000-of-00001.parquet").as_posix()
                    connection.execute(
                        f"""
                        COPY (
                            SELECT * FROM (VALUES
                                (0, 'int main() {{ return 0; }}', '1'),
                                (1, 'int main() {{ return 1; }}', '1'),
                                (2, 'int main() {{ return 2; }}', '2'),
                                (3, 'int main() {{ return 3; }}', '2')
                            ) AS rows(id, code, label)
                        ) TO '{path}' (FORMAT PARQUET)
                        """
                    )
            finally:
                connection.close()

            database = root / "poj.duckdb"
            benchmark.bundle_poj(
                argparse.Namespace(
                    parquet_root=parquet_root, database=database, replace=False
                )
            )
            connection = duckdb.connect(str(database))
            try:
                count, distinct_ids = connection.execute(
                    "SELECT count(*), count(DISTINCT submission_id) FROM submissions"
                ).fetchone()
            finally:
                connection.close()

            self.assertEqual(count, 8)
            self.assertEqual(distinct_ids, 8)

    def test_evaluate_writes_auc_and_threshold_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            results = root / "Benchmark-Result.csv"
            results.write_text(
                "Label,Score\n1,90\n0,80\n1,70\n0,10\n",
                encoding="utf-8",
            )
            output = root / "metrics.json"
            with contextlib.redirect_stdout(io.StringIO()):
                benchmark.evaluate(
                    argparse.Namespace(
                        results=results, output=output, threshold=[50.0]
                    )
                )
            report = json.loads(output.read_text(encoding="utf-8"))

            self.assertAlmostEqual(report["roc_auc"], 0.75)
            self.assertAlmostEqual(report["pr_auc_average_precision"], 5 / 6)
            self.assertEqual(report["requested_thresholds"][0]["threshold"], 50.0)


if __name__ == "__main__":
    unittest.main()