from __future__ import annotations

import argparse
import bisect
import csv
import json
import random
import shutil
from dataclasses import dataclass
from pathlib import Path

import duckdb


@dataclass(frozen=True, slots=True)
class Submission:
    problem_id: str
    submission_id: str
    source_bytes: int


def connect(database: Path) -> duckdb.DuckDBPyConnection:
    return duckdb.connect(str(database))


def bundle(args: argparse.Namespace) -> None:
    source_root = args.source_root.resolve()
    database = args.database.resolve()
    if not source_root.is_dir():
        raise SystemExit(f"source root does not exist: {source_root}")
    if database.exists():
        if not args.replace:
            raise SystemExit(f"database already exists: {database} (use --replace)")
        database.unlink()

    database.parent.mkdir(parents=True, exist_ok=True)
    problem_directories = sorted(
        path
        for path in source_root.iterdir()
        if path.is_dir() and len(path.name) == 6 and path.name.startswith("p")
    )
    if not problem_directories:
        raise SystemExit(f"source root contains no problem directories: {source_root}")
    temporary_directory = database.with_suffix(database.suffix + ".tmp")
    shutil.rmtree(temporary_directory, ignore_errors=True)
    temporary_directory.mkdir()
    connection = connect(database)
    try:
        connection.execute("SET threads = 2")
        connection.execute("SET memory_limit = '2GB'")
        connection.execute("SET preserve_insertion_order = false")
        connection.execute(
            "SET temp_directory = ?", [temporary_directory.as_posix()]
        )
        connection.execute(
            """
            CREATE TABLE submissions (
                problem_id VARCHAR NOT NULL,
                submission_id VARCHAR NOT NULL,
                source VARCHAR NOT NULL,
                source_bytes UINTEGER NOT NULL
            )
            """
        )
        batch_size = 100
        for start in range(0, len(problem_directories), batch_size):
            batch = problem_directories[start : start + batch_size]
            connection.execute(
                """
                INSERT INTO submissions
                SELECT
                    regexp_extract(replace(filename, '\\', '/'), '/(p[0-9]{5})/', 1),
                    regexp_extract(replace(filename, '\\', '/'), '/([^/]+)\\.cpp$', 1),
                    decode(content),
                    size::UINTEGER
                FROM read_blob(?)
                """,
                [[(problem_directory / "*.cpp").as_posix() for problem_directory in batch]],
            )
            completed = start + len(batch)
            print(
                f"Bundled {completed}/{len(problem_directories)} problem directories",
                flush=True,
            )
        invalid = connection.execute(
            "SELECT count(*) FROM submissions WHERE problem_id = '' OR submission_id = ''"
        ).fetchone()[0]
        if invalid:
            raise RuntimeError(f"failed to parse identities for {invalid} source files")
        duplicate_ids = connection.execute(
            "SELECT count(*) - count(DISTINCT submission_id) FROM submissions"
        ).fetchone()[0]
        if duplicate_ids:
            raise RuntimeError(f"found {duplicate_ids} duplicate submission IDs")
        connection.execute(
            """
            CREATE TABLE benchmark_pairs (
                pair_id BIGINT PRIMARY KEY,
                left_id VARCHAR NOT NULL,
                right_id VARCHAR NOT NULL,
                label BOOLEAN NOT NULL,
                kind VARCHAR NOT NULL,
                split VARCHAR NOT NULL,
                size_delta INTEGER NOT NULL
            )
            """
        )
        connection.execute("CHECKPOINT")
        problems, submissions, source_bytes = connection.execute(
            "SELECT count(DISTINCT problem_id), count(*), sum(source_bytes) FROM submissions"
        ).fetchone()
    finally:
        connection.close()
        shutil.rmtree(temporary_directory, ignore_errors=True)

    print(
        json.dumps(
            {
                "database": str(database),
                "problems": problems,
                "submissions": submissions,
                "source_bytes": source_bytes,
                "database_bytes": database.stat().st_size,
            },
            indent=2,
        )
    )


def load_submissions(
    connection: duckdb.DuckDBPyConnection,
) -> tuple[dict[str, list[Submission]], list[Submission]]:
    by_problem: dict[str, list[Submission]] = {}
    all_submissions: list[Submission] = []
    cursor = connection.execute(
        "SELECT problem_id, submission_id, source_bytes FROM submissions ORDER BY problem_id, submission_id"
    )
    while rows := cursor.fetchmany(10_000):
        for problem_id, submission_id, source_bytes in rows:
            submission = Submission(problem_id, submission_id, source_bytes)
            by_problem.setdefault(problem_id, []).append(submission)
            all_submissions.append(submission)
    return by_problem, all_submissions


def canonical_pair(left: Submission, right: Submission) -> tuple[str, str]:
    return tuple(sorted((left.submission_id, right.submission_id)))


def nearest_negative(
    left: Submission,
    sorted_submissions: list[Submission],
    sorted_sizes: list[int],
    used_pairs: set[tuple[str, str]],
    generator: random.Random,
) -> Submission:
    center = bisect.bisect_left(sorted_sizes, left.source_bytes)
    radius = min(32, len(sorted_submissions))
    while radius:
        start = max(0, center - radius)
        end = min(len(sorted_submissions), center + radius)
        candidates = [
            candidate
            for candidate in sorted_submissions[start:end]
            if candidate.problem_id != left.problem_id
            and canonical_pair(left, candidate) not in used_pairs
        ]
        if candidates:
            candidates.sort(key=lambda candidate: abs(candidate.source_bytes - left.source_bytes))
            return generator.choice(candidates[: min(32, len(candidates))])
        if radius == len(sorted_submissions):
            break
        radius = min(radius * 2, len(sorted_submissions))
    raise RuntimeError(f"could not find a negative pair for {left.submission_id}")


def sample_pairs(args: argparse.Namespace) -> None:
    generator = random.Random(args.seed)
    connection = connect(args.database.resolve())
    try:
        by_problem, all_submissions = load_submissions(connection)
        sorted_submissions = sorted(
            all_submissions,
            key=lambda submission: (submission.source_bytes, submission.submission_id),
        )
        sorted_sizes = [submission.source_bytes for submission in sorted_submissions]
        used_pairs: set[tuple[str, str]] = set()
        pairs: list[tuple[str, str, bool, str, str, int]] = []

        for problem_id in sorted(by_problem):
            submissions = by_problem[problem_id]
            if len(submissions) < 2:
                continue
            available_positive_pairs = len(submissions) * (len(submissions) - 1) // 2
            if args.positive_per_problem > available_positive_pairs:
                raise SystemExit(
                    f"problem {problem_id} has only {available_positive_pairs} unique pairs"
                )

            positive_count = 0
            while positive_count < args.positive_per_problem:
                left, right = generator.sample(submissions, 2)
                key = canonical_pair(left, right)
                if key in used_pairs:
                    continue
                used_pairs.add(key)
                pairs.append(
                    (
                        left.submission_id,
                        right.submission_id,
                        True,
                        "same-problem",
                        args.split,
                        abs(left.source_bytes - right.source_bytes),
                    )
                )
                positive_count += 1

            for _ in range(args.negative_per_problem):
                left = generator.choice(submissions)
                right = nearest_negative(
                    left, sorted_submissions, sorted_sizes, used_pairs, generator
                )
                used_pairs.add(canonical_pair(left, right))
                pairs.append(
                    (
                        left.submission_id,
                        right.submission_id,
                        False,
                        "different-problem-size-matched",
                        args.split,
                        abs(left.source_bytes - right.source_bytes),
                    )
                )

        generator.shuffle(pairs)
        connection.execute("DELETE FROM benchmark_pairs")
        connection.executemany(
            "INSERT INTO benchmark_pairs VALUES (?, ?, ?, ?, ?, ?, ?)",
            [
                (pair_id, left, right, label, kind, split, size_delta)
                for pair_id, (left, right, label, kind, split, size_delta) in enumerate(
                    pairs, start=1
                )
            ],
        )
        connection.execute("CHECKPOINT")
    finally:
        connection.close()

    print(
        json.dumps(
            {
                "database": str(args.database.resolve()),
                "seed": args.seed,
                "pairs": len(pairs),
                "positive": sum(1 for pair in pairs if pair[2]),
                "negative": sum(1 for pair in pairs if not pair[2]),
            },
            indent=2,
        )
    )


def materialized_name(problem_id: str, submission_id: str) -> str:
    return f"{problem_id}__{submission_id}.cpp"


def materialize(args: argparse.Namespace) -> None:
    output = args.output.resolve()
    if output.exists():
        if not args.replace:
            raise SystemExit(f"output already exists: {output} (use --replace)")
        shutil.rmtree(output)
    source_output = output / "sources"
    source_output.mkdir(parents=True)

    connection = connect(args.database.resolve())
    try:
        pair_rows = connection.execute(
            """
            SELECT
                bp.pair_id,
                lp.problem_id,
                bp.left_id,
                rp.problem_id,
                bp.right_id,
                bp.label,
                bp.kind,
                bp.split
            FROM benchmark_pairs bp
            JOIN submissions lp ON lp.submission_id = bp.left_id
            JOIN submissions rp ON rp.submission_id = bp.right_id
            ORDER BY bp.pair_id
            """
        ).fetchall()
        if not pair_rows:
            raise SystemExit("benchmark_pairs is empty; run sample first")

        source_rows = connection.execute(
            """
            SELECT problem_id, submission_id, source
            FROM submissions
            WHERE submission_id IN (
                SELECT left_id FROM benchmark_pairs
                UNION
                SELECT right_id FROM benchmark_pairs
            )
            ORDER BY problem_id, submission_id
            """
        )
        source_count = 0
        while rows := source_rows.fetchmany(1_000):
            for problem_id, submission_id, source in rows:
                (source_output / materialized_name(problem_id, submission_id)).write_bytes(
                    source.encode("utf-8")
                )
                source_count += 1
    finally:
        connection.close()

    manifest = output / "pairs.csv"
    with manifest.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["left_dna", "right_dna", "label", "pair_id", "kind", "split"])
        for pair_id, left_problem, left_id, right_problem, right_id, label, kind, split in pair_rows:
            writer.writerow(
                [
                    materialized_name(left_problem, left_id) + ".DNA",
                    materialized_name(right_problem, right_id) + ".DNA",
                    int(label),
                    pair_id,
                    kind,
                    split,
                ]
            )

    summary = {
        "database": str(args.database.resolve()),
        "sources": source_count,
        "pairs": len(pair_rows),
        "positive": sum(1 for row in pair_rows if row[5]),
        "negative": sum(1 for row in pair_rows if not row[5]),
    }
    (output / "summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2))


def status(args: argparse.Namespace) -> None:
    connection = connect(args.database.resolve())
    try:
        summary = connection.execute(
            """
            SELECT
                count(DISTINCT problem_id) AS problems,
                count(*) AS submissions,
                sum(source_bytes) AS source_bytes
            FROM submissions
            """
        ).fetchone()
        pairs = connection.execute(
            """
            SELECT count(*), count(*) FILTER (WHERE label), count(*) FILTER (WHERE NOT label)
            FROM benchmark_pairs
            """
        ).fetchone()
    finally:
        connection.close()
    print(
        json.dumps(
            {
                "database": str(args.database.resolve()),
                "database_bytes": args.database.resolve().stat().st_size,
                "problems": summary[0],
                "submissions": summary[1],
                "source_bytes": summary[2],
                "pairs": pairs[0],
                "positive": pairs[1],
                "negative": pairs[2],
            },
            indent=2,
        )
    )


def classification_metrics(
    scores: list[tuple[float, bool]], threshold: float
) -> dict[str, float | int]:
    true_positive = sum(1 for score, label in scores if score >= threshold and label)
    false_positive = sum(1 for score, label in scores if score >= threshold and not label)
    false_negative = sum(1 for score, label in scores if score < threshold and label)
    true_negative = sum(1 for score, label in scores if score < threshold and not label)
    precision = true_positive / (true_positive + false_positive) if true_positive + false_positive else 0.0
    recall = true_positive / (true_positive + false_negative) if true_positive + false_negative else 0.0
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return {
        "threshold": threshold,
        "true_positive": true_positive,
        "false_positive": false_positive,
        "false_negative": false_negative,
        "true_negative": true_negative,
        "precision": precision,
        "recall": recall,
        "f1": f1,
    }


def evaluate(args: argparse.Namespace) -> None:
    with args.results.resolve().open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        required = {"Label", "Score"}
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise SystemExit("benchmark result must contain Label and Score columns")
        scores = [(float(row["Score"]), row["Label"] == "1") for row in reader]
    if not scores:
        raise SystemExit("benchmark result contains no rows")
    if not any(label for _, label in scores) or all(label for _, label in scores):
        raise SystemExit("benchmark result must contain both positive and negative labels")

    thresholds = sorted({score for score, _ in scores} | {0.0, 100.0})
    all_metrics = [classification_metrics(scores, threshold) for threshold in thresholds]
    best = max(
        all_metrics,
        key=lambda metrics: (metrics["f1"], metrics["precision"], metrics["threshold"]),
    )
    requested = [classification_metrics(scores, threshold) for threshold in args.threshold]
    report = {
        "results": str(args.results.resolve()),
        "pairs": len(scores),
        "positive": sum(1 for _, label in scores if label),
        "negative": sum(1 for _, label in scores if not label),
        "best_f1": best,
        "requested_thresholds": requested,
    }
    serialized = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.resolve().parent.mkdir(parents=True, exist_ok=True)
        args.output.resolve().write_text(serialized, encoding="utf-8")
    print(serialized, end="")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Bundle and sample Project CodeNet C++ benchmarks with DuckDB."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    bundle_parser = subparsers.add_parser("bundle", help="Create a DuckDB corpus bundle.")
    bundle_parser.add_argument("source_root", type=Path)
    bundle_parser.add_argument("database", type=Path)
    bundle_parser.add_argument("--replace", action="store_true")
    bundle_parser.set_defaults(handler=bundle)

    sample_parser = subparsers.add_parser("sample", help="Create deterministic benchmark pairs.")
    sample_parser.add_argument("database", type=Path)
    sample_parser.add_argument("--positive-per-problem", type=int, default=10)
    sample_parser.add_argument("--negative-per-problem", type=int, default=10)
    sample_parser.add_argument("--seed", type=int, default=20260720)
    sample_parser.add_argument("--split", default="evaluation")
    sample_parser.set_defaults(handler=sample_pairs)

    materialize_parser = subparsers.add_parser(
        "materialize", help="Write sampled sources and the pair manifest."
    )
    materialize_parser.add_argument("database", type=Path)
    materialize_parser.add_argument("output", type=Path)
    materialize_parser.add_argument("--replace", action="store_true")
    materialize_parser.set_defaults(handler=materialize)

    status_parser = subparsers.add_parser("status", help="Show bundle statistics.")
    status_parser.add_argument("database", type=Path)
    status_parser.set_defaults(handler=status)

    evaluate_parser = subparsers.add_parser(
        "evaluate", help="Evaluate a labeled Benchmark-Result.csv."
    )
    evaluate_parser.add_argument("results", type=Path)
    evaluate_parser.add_argument(
        "--threshold", type=float, action="append", default=[50.0, 70.0, 90.0]
    )
    evaluate_parser.add_argument("--output", type=Path)
    evaluate_parser.set_defaults(handler=evaluate)

    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if getattr(args, "positive_per_problem", 0) < 0:
        raise SystemExit("--positive-per-problem must be non-negative")
    if getattr(args, "negative_per_problem", 0) < 0:
        raise SystemExit("--negative-per-problem must be non-negative")
    args.handler(args)


if __name__ == "__main__":
    main()