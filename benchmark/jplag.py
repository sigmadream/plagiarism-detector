"""Run JPlag on a materialized source directory and map its scores onto a cppTR pair manifest.

The output follows the cppTR ``compare-manifest`` layout closely enough that
``benchmark/benchmark.py evaluate`` can consume it directly (it only needs the
``Label`` and ``Score`` columns).

Example:

    uv run python benchmark/jplag.py run \
        benchmark/work/poj104/validation/sources \
        benchmark/work/poj104/validation/pairs.csv \
        benchmark/work/poj104/validation/results-jplag \
        --java <path to java 25> --chunk-files 150

JPlag 6.3.0 requires Java 25. Keep the sources, the output directory and the
working directory on the same drive: JPlag's report writer relativizes paths and
fails across Windows drive letters.
"""

from __future__ import annotations

import argparse
import csv
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_JAR = Path(__file__).resolve().parent / "jplag-6.3.0-jar-with-dependencies.jar"
MANIFEST_HEADER = ["left_dna", "right_dna", "label", "pair_id", "kind", "split"]
RESULT_HEADER = [
    "PairId", "Label", "Kind", "Split", "Program1", "Program2",
    "Score", "MaxScore", "Missing",
]


def strip_dna_suffix(name: str) -> str:
    return name[:-4] if name.endswith(".DNA") else name


def read_manifest(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.reader(stream)
        header = next(reader)
        if header != MANIFEST_HEADER:
            raise SystemExit(f"unsupported manifest header in {path}: {header}")
        rows = []
        for row in reader:
            if not row:
                continue
            rows.append(dict(zip(MANIFEST_HEADER, row)))
    return rows


def run_jplag(
    java: str,
    jar: Path,
    sources: Path,
    result_dir: Path,
    language: str,
    min_tokens: int | None,
    extra_args: list[str],
) -> tuple[Path, float, str]:
    if result_dir.exists():
        shutil.rmtree(result_dir)
    zip_path = result_dir.with_suffix(".jplag")
    if zip_path.exists():
        zip_path.unlink()
    result_dir.parent.mkdir(parents=True, exist_ok=True)

    command = [
        java, "-jar", str(jar),
        "-M", "RUN",
        "-l", language,
        "--csv-export",
        "-m", "0",
        "-n", "-1",
        "--overwrite",
        "-r", str(result_dir),
    ]
    if min_tokens is not None:
        command += ["-t", str(min_tokens)]
    command += extra_args
    command.append(str(sources))

    started = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=str(result_dir.parent),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    elapsed = time.perf_counter() - started
    log = completed.stdout + "\n" + completed.stderr
    csv_path = result_dir / "results.csv"
    if not csv_path.is_file():
        sys.stderr.write(log[-4000:])
        raise SystemExit(f"JPlag did not produce {csv_path} (exit code {completed.returncode})")
    return csv_path, elapsed, log


def load_similarities(csv_path: Path) -> dict[frozenset[str], tuple[float, float]]:
    scores: dict[frozenset[str], tuple[float, float]] = {}
    with csv_path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        for row in reader:
            key = frozenset((row["submissionName1"], row["submissionName2"]))
            scores[key] = (
                float(row["averageSimilarity"]) * 100.0,
                float(row["maxSimilarity"]) * 100.0,
            )
    return scores


def chunk_manifest(manifest: list[dict[str, str]], max_files: int) -> list[set[str]]:
    """Greedily group manifest pairs so that each group references at most ``max_files`` files.

    JPlag's pairwise similarity does not depend on the other submissions in a run (no base
    code, no normalization), so running it per group yields the same scores as one full run
    while avoiding the all-pairs blow-up on large corpora.
    """
    groups: list[set[str]] = []
    for row in manifest:
        left = strip_dna_suffix(row["left_dna"])
        right = strip_dna_suffix(row["right_dna"])
        placed = False
        for group in groups:
            needed = {left, right} - group
            if len(group) + len(needed) <= max_files:
                group |= needed
                placed = True
                break
        if not placed:
            groups.append({left, right})
    return groups


def run_chunked(
    args: argparse.Namespace,
    jar: Path,
    sources: Path,
    output: Path,
    manifest: list[dict[str, str]],
) -> tuple[dict[frozenset[str], tuple[float, float]], float, int]:
    groups = chunk_manifest(manifest, args.chunk_files)
    scores: dict[frozenset[str], tuple[float, float]] = {}
    total_elapsed = 0.0
    chunk_root = output / "chunks"
    if chunk_root.exists():
        shutil.rmtree(chunk_root)
    for index, group in enumerate(groups, start=1):
        chunk_dir = chunk_root / f"chunk_{index:03d}"
        chunk_sources = chunk_dir / "sources"
        chunk_sources.mkdir(parents=True)
        for name in sorted(group):
            shutil.copyfile(sources / name, chunk_sources / name)
        csv_path, elapsed, log = run_jplag(
            args.java, jar, chunk_sources, chunk_dir / "jplag", args.language, args.min_tokens, args.jplag_arg or [],
        )
        (chunk_dir / "jplag.log").write_text(log, encoding="utf-8")
        scores.update(load_similarities(csv_path))
        total_elapsed += elapsed
        print(f"chunk {index}/{len(groups)}: {len(group)} files, {elapsed:.1f} s", file=sys.stderr, flush=True)
        shutil.rmtree(chunk_sources)
    return scores, total_elapsed, len(groups)


def command_run(args: argparse.Namespace) -> None:
    sources = Path(args.sources).resolve()
    manifest_path = Path(args.manifest).resolve()
    output = Path(args.output).resolve()
    jar = Path(args.jar).resolve()
    if not jar.is_file():
        raise SystemExit(f"JPlag jar not found: {jar}")
    if not sources.is_dir():
        raise SystemExit(f"source directory not found: {sources}")

    manifest = read_manifest(manifest_path)
    output.mkdir(parents=True, exist_ok=True)
    chunks = 1
    if args.chunk_files:
        scores, elapsed, chunks = run_chunked(args, jar, sources, output, manifest)
    else:
        csv_path, elapsed, log = run_jplag(
            args.java, jar, sources, output / "jplag", args.language, args.min_tokens, args.jplag_arg or [],
        )
        (output / "jplag.log").write_text(log, encoding="utf-8")
        scores = load_similarities(csv_path)

    missing = 0
    result_path = output / "Benchmark-Result.csv"
    with result_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(RESULT_HEADER)
        for row in manifest:
            left = strip_dna_suffix(row["left_dna"])
            right = strip_dna_suffix(row["right_dna"])
            found = scores.get(frozenset((left, right)))
            if found is None:
                missing += 1
                average, maximum, flag = 0.0, 0.0, 1
            else:
                average, maximum, flag = found[0], found[1], 0
            writer.writerow([
                row["pair_id"], row["label"], row["kind"], row["split"],
                left, right, f"{average:.6f}", f"{maximum:.6f}", flag,
            ])

    summary = {
        "tool": "jplag-6.3.0",
        "language": args.language,
        "min_tokens": args.min_tokens,
        "extra_args": args.jplag_arg or [],
        "sources": str(sources),
        "source_files": sum(1 for p in sources.iterdir() if p.is_file()),
        "jplag_comparisons": len(scores),
        "manifest_pairs": len(manifest),
        "missing_pairs": missing,
        "jplag_seconds": round(elapsed, 3),
        "chunk_files": args.chunk_files,
        "chunks": chunks,
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    if missing:
        print(f"warning: {missing} manifest pairs had no JPlag comparison; scored as 0", file=sys.stderr)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest="command", required=True)
    run = subparsers.add_parser("run", help="Run JPlag and write a manifest-aligned result CSV.")
    run.add_argument("sources", help="Directory whose files are the submissions (one file per submission).")
    run.add_argument("manifest", help="cppTR pair manifest (left_dna,right_dna,label,pair_id,kind,split).")
    run.add_argument("output", help="Output directory for Benchmark-Result.csv, summary.json and the JPlag report.")
    run.add_argument("--jar", default=str(DEFAULT_JAR))
    run.add_argument("--java", default="java", help="Java 25 executable.")
    run.add_argument("--language", default="cpp")
    run.add_argument("--min-tokens", type=int, default=None, help="JPlag -t (default: JPlag's language default).")
    run.add_argument("--jplag-arg", action="append", help="Extra argument passed verbatim to JPlag (repeatable).")
    run.add_argument(
        "--chunk-files", type=int, default=0,
        help="Split the manifest into groups of at most this many files and run JPlag per group "
             "(0 = one run over the whole directory).",
    )
    run.set_defaults(func=command_run)
    return parser


def main(argv: list[str] | None = None) -> None:
    args = build_parser().parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
