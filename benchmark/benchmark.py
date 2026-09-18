from __future__ import annotations

import argparse
import bisect
import csv
import json
import random
import re
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
    if not database.is_file():
        raise SystemExit(f"database does not exist: {database}")
    return duckdb.connect(str(database))


def create_database(database: Path, replace: bool) -> duckdb.DuckDBPyConnection:
    if database.exists():
        if not replace:
            raise SystemExit(f"database already exists: {database} (use --replace)")
        database.unlink()
    database.parent.mkdir(parents=True, exist_ok=True)
    connection = duckdb.connect(str(database))
    connection.execute("SET threads = 2")
    connection.execute("SET memory_limit = '2GB'")
    connection.execute("SET preserve_insertion_order = false")
    connection.execute(
        """
        CREATE TABLE submissions (
            corpus VARCHAR NOT NULL,
            split VARCHAR NOT NULL,
            problem_id VARCHAR NOT NULL,
            submission_id VARCHAR PRIMARY KEY,
            source VARCHAR NOT NULL,
            source_bytes UINTEGER NOT NULL
        )
        """
    )
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
    return connection


def corpus_summary(connection: duckdb.DuckDBPyConnection, database: Path) -> dict[str, object]:
    corpus, problems, submissions, source_bytes = connection.execute(
        """
        SELECT min(corpus), count(DISTINCT problem_id), count(*), sum(source_bytes)
        FROM submissions
        """
    ).fetchone()
    splits = dict(
        connection.execute(
            "SELECT split, count(*) FROM submissions GROUP BY split ORDER BY split"
        ).fetchall()
    )
    return {
        "database": str(database),
        "corpus": corpus,
        "problems": problems,
        "submissions": submissions,
        "source_bytes": source_bytes,
        "splits": splits,
    }


def bundle_poj(args: argparse.Namespace) -> None:
    parquet_root = args.parquet_root.resolve()
    database = args.database.resolve()
    if not parquet_root.is_dir():
        raise SystemExit(f"Parquet root does not exist: {parquet_root}")
    parquet_files = sorted(parquet_root.glob("*.parquet"))
    if not parquet_files:
        raise SystemExit(f"Parquet root contains no files: {parquet_root}")

    connection = create_database(database, args.replace)
    try:
        connection.execute(
            """
            INSERT INTO submissions
            SELECT
                'poj104',
                regexp_extract(
                    replace(filename, '\\', '/'),
                    '/(train|validation|test)-',
                    1
                ) AS split,
                label AS problem_id,
                split || ':' || id::VARCHAR AS submission_id,
                code AS source,
                octet_length(encode(code))::UINTEGER AS source_bytes
            FROM read_parquet(?, filename = true)
            """,
            [[path.as_posix() for path in parquet_files]],
        )
        invalid = connection.execute(
            """
            SELECT count(*) FROM submissions
            WHERE split = '' OR problem_id = '' OR source = ''
            """
        ).fetchone()[0]
        if invalid:
            raise RuntimeError(f"found {invalid} invalid POJ-104 rows")
        connection.execute("CHECKPOINT")
        summary = corpus_summary(connection, database)
    finally:
        connection.close()

    summary["database_bytes"] = database.stat().st_size
    print(json.dumps(summary, indent=2))


def resolve_split(connection: duckdb.DuckDBPyConnection, requested: str | None) -> str:
    splits = [row[0] for row in connection.execute(
        "SELECT DISTINCT split FROM submissions ORDER BY split"
    ).fetchall()]
    if requested:
        if requested not in splits:
            raise SystemExit(
                f"split {requested!r} does not exist; available: {', '.join(splits)}"
            )
        return requested
    if len(splits) == 1:
        return splits[0]
    raise SystemExit(f"--split is required; available: {', '.join(splits)}")


def load_submissions(
    connection: duckdb.DuckDBPyConnection, split: str
) -> tuple[dict[str, list[Submission]], list[Submission]]:
    by_problem: dict[str, list[Submission]] = {}
    all_submissions: list[Submission] = []
    cursor = connection.execute(
        """
        SELECT problem_id, submission_id, source_bytes
        FROM submissions
        WHERE split = ?
        ORDER BY problem_id, submission_id
        """,
        [split],
    )
    while rows := cursor.fetchmany(10_000):
        for problem_id, submission_id, source_bytes in rows:
            submission = Submission(problem_id, submission_id, source_bytes)
            by_problem.setdefault(problem_id, []).append(submission)
            all_submissions.append(submission)
    if len(by_problem) < 2:
        raise SystemExit(f"split {split!r} must contain at least two problems")
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
            candidates.sort(
                key=lambda candidate: abs(candidate.source_bytes - left.source_bytes)
            )
            return generator.choice(candidates[: min(32, len(candidates))])
        if radius == len(sorted_submissions):
            break
        radius = min(radius * 2, len(sorted_submissions))
    raise RuntimeError(f"could not find a negative pair for {left.submission_id}")


def sample(args: argparse.Namespace) -> None:
    if args.positive_per_problem < 1 or args.negative_per_problem < 1:
        raise SystemExit("pair counts must be positive")
    generator = random.Random(args.seed)
    database = args.database.resolve()
    connection = connect(database)
    try:
        split = resolve_split(connection, args.split)
        by_problem, all_submissions = load_submissions(connection, split)
        sorted_submissions = sorted(
            all_submissions,
            key=lambda submission: (submission.source_bytes, submission.submission_id),
        )
        sorted_sizes = [submission.source_bytes for submission in sorted_submissions]
        used_pairs: set[tuple[str, str]] = set()
        pairs: list[tuple[str, str, bool, str, str, int]] = []

        for problem_id in sorted(by_problem):
            submissions = by_problem[problem_id]
            available = len(submissions) * (len(submissions) - 1) // 2
            if args.positive_per_problem > available:
                raise SystemExit(
                    f"problem {problem_id} has only {available} unique positive pairs"
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
                        split,
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
                        split,
                        abs(left.source_bytes - right.source_bytes),
                    )
                )

        generator.shuffle(pairs)
        connection.execute("DELETE FROM benchmark_pairs")
        connection.executemany(
            "INSERT INTO benchmark_pairs VALUES (?, ?, ?, ?, ?, ?, ?)",
            [
                (pair_id, *pair)
                for pair_id, pair in enumerate(pairs, start=1)
            ],
        )
        connection.execute("CHECKPOINT")
    finally:
        connection.close()

    print(
        json.dumps(
            {
                "database": str(database),
                "split": split,
                "seed": args.seed,
                "pairs": len(pairs),
                "positive": sum(1 for pair in pairs if pair[2]),
                "negative": sum(1 for pair in pairs if not pair[2]),
            },
            indent=2,
        )
    )


def safe_component(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("_")


def materialized_name(corpus: str, problem_id: str, submission_id: str) -> str:
    return "__".join(
        safe_component(value) for value in (corpus, problem_id, submission_id)
    ) + ".cpp"


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
                lp.corpus,
                lp.problem_id,
                bp.left_id,
                rp.corpus,
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
            SELECT corpus, problem_id, submission_id, source
            FROM submissions
            WHERE submission_id IN (
                SELECT left_id FROM benchmark_pairs
                UNION
                SELECT right_id FROM benchmark_pairs
            )
            ORDER BY submission_id
            """
        )
        source_names: set[str] = set()
        source_count = 0
        while rows := source_rows.fetchmany(1_000):
            for corpus, problem_id, submission_id, source in rows:
                name = materialized_name(corpus, problem_id, submission_id)
                if name in source_names:
                    raise RuntimeError(f"materialized filename collision: {name}")
                source_names.add(name)
                (source_output / name).write_bytes(source.encode("utf-8"))
                source_count += 1
    finally:
        connection.close()

    manifest = output / "pairs.csv"
    with manifest.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(
            ["left_dna", "right_dna", "label", "pair_id", "kind", "split"]
        )
        for row in pair_rows:
            pair_id, left_corpus, left_problem, left_id = row[:4]
            right_corpus, right_problem, right_id = row[4:7]
            label, kind, split = row[7:]
            writer.writerow(
                [
                    materialized_name(left_corpus, left_problem, left_id) + ".DNA",
                    materialized_name(right_corpus, right_problem, right_id) + ".DNA",
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
        "positive": sum(1 for row in pair_rows if row[7]),
        "negative": sum(1 for row in pair_rows if not row[7]),
    }
    (output / "summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2))


def status(args: argparse.Namespace) -> None:
    database = args.database.resolve()
    connection = connect(database)
    try:
        summary = corpus_summary(connection, database)
        pairs = connection.execute(
            """
            SELECT count(*),
                   count(*) FILTER (WHERE label),
                   count(*) FILTER (WHERE NOT label)
            FROM benchmark_pairs
            """
        ).fetchone()
    finally:
        connection.close()
    summary.update(
        {
            "database_bytes": database.stat().st_size,
            "pairs": pairs[0],
            "positive": pairs[1],
            "negative": pairs[2],
        }
    )
    print(json.dumps(summary, indent=2))


def classification_metrics(
    scores: list[tuple[float, bool]], threshold: float
) -> dict[str, float | int]:
    true_positive = sum(1 for score, label in scores if score >= threshold and label)
    false_positive = sum(1 for score, label in scores if score >= threshold and not label)
    false_negative = sum(1 for score, label in scores if score < threshold and label)
    true_negative = sum(1 for score, label in scores if score < threshold and not label)
    precision = (
        true_positive / (true_positive + false_positive)
        if true_positive + false_positive
        else 0.0
    )
    recall = (
        true_positive / (true_positive + false_negative)
        if true_positive + false_negative
        else 0.0
    )
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


def roc_auc(scores: list[tuple[float, bool]]) -> float:
    ordered = sorted(scores, key=lambda item: item[0])
    positive_count = sum(label for _, label in ordered)
    negative_count = len(ordered) - positive_count
    positive_rank_sum = 0.0
    index = 0
    while index < len(ordered):
        end = index + 1
        while end < len(ordered) and ordered[end][0] == ordered[index][0]:
            end += 1
        average_rank = ((index + 1) + end) / 2
        positive_rank_sum += average_rank * sum(label for _, label in ordered[index:end])
        index = end
    return (
        positive_rank_sum - positive_count * (positive_count + 1) / 2
    ) / (positive_count * negative_count)


def average_precision(scores: list[tuple[float, bool]]) -> float:
    ordered = sorted(scores, key=lambda item: item[0], reverse=True)
    positive_count = sum(label for _, label in ordered)
    true_positive = 0
    previous_recall = 0.0
    area = 0.0
    index = 0
    while index < len(ordered):
        end = index + 1
        while end < len(ordered) and ordered[end][0] == ordered[index][0]:
            end += 1
        true_positive += sum(label for _, label in ordered[index:end])
        recall = true_positive / positive_count
        precision = true_positive / end
        area += (recall - previous_recall) * precision
        previous_recall = recall
        index = end
    return area


def evaluate(args: argparse.Namespace) -> None:
    results = args.results.resolve()
    with results.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        required = {"Label", "Score"}
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise SystemExit("benchmark result must contain Label and Score columns")
        scores = [(float(row["Score"]), row["Label"] == "1") for row in reader]
    if not scores:
        raise SystemExit("benchmark result contains no rows")
    if not any(label for _, label in scores) or all(label for _, label in scores):
        raise SystemExit("benchmark result must contain positive and negative labels")

    thresholds = sorted({score for score, _ in scores} | {0.0, 100.0})
    all_metrics = [classification_metrics(scores, threshold) for threshold in thresholds]
    best = max(
        all_metrics,
        key=lambda metrics: (metrics["f1"], metrics["precision"], metrics["threshold"]),
    )
    report = {
        "results": str(results),
        "pairs": len(scores),
        "positive": sum(label for _, label in scores),
        "negative": sum(not label for _, label in scores),
        "roc_auc": roc_auc(scores),
        "pr_auc_average_precision": average_precision(scores),
        "best_f1_exploratory": best,
        "requested_thresholds": [
            classification_metrics(scores, threshold) for threshold in args.threshold
        ],
    }
    serialized = json.dumps(report, indent=2) + "\n"
    if args.output:
        output = args.output.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(serialized, encoding="utf-8")
    print(serialized, end="")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the cppTR semantic similarity benchmark on POJ-104."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    poj_parser = subparsers.add_parser(
        "bundle-poj", help="Bundle the POJ-104 Parquet corpus."
    )
    poj_parser.add_argument("parquet_root", type=Path)
    poj_parser.add_argument("database", type=Path)
    poj_parser.add_argument("--replace", action="store_true")
    poj_parser.set_defaults(handler=bundle_poj)

    sample_parser = subparsers.add_parser(
        "sample", help="Create deterministic positive and negative pairs."
    )
    sample_parser.add_argument("database", type=Path)
    sample_parser.add_argument("--split")
    sample_parser.add_argument("--positive-per-problem", type=int, default=10)
    sample_parser.add_argument("--negative-per-problem", type=int, default=10)
    sample_parser.add_argument("--seed", type=int, default=20260720)
    sample_parser.set_defaults(handler=sample)

    materialize_parser = subparsers.add_parser(
        "materialize", help="Write selected sources and the comparison manifest."
    )
    materialize_parser.add_argument("database", type=Path)
    materialize_parser.add_argument("output", type=Path)
    materialize_parser.add_argument("--replace", action="store_true")
    materialize_parser.set_defaults(handler=materialize)

    status_parser = subparsers.add_parser("status", help="Show corpus and pair counts.")
    status_parser.add_argument("database", type=Path)
    status_parser.set_defaults(handler=status)

    evaluate_parser = subparsers.add_parser(
        "evaluate", help="Calculate classification and ranking metrics."
    )
    evaluate_parser.add_argument("results", type=Path)
    evaluate_parser.add_argument("--output", type=Path)
    evaluate_parser.add_argument(
        "--threshold", type=float, action="append", default=[50.0, 70.0, 90.0]
    )
    evaluate_parser.set_defaults(handler=evaluate)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.handler(args)


if __name__ == "__main__":
    main()