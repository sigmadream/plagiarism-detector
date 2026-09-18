"""Problem-clustered paired bootstrap for benchmark result CSVs.

Pairs that share a problem are not independent, so the bootstrap resamples problems
(clusters) with replacement and keeps every pair whose sampling problem was drawn. The
same resampled clusters are applied to every system, which makes the metric differences
paired.

Example:

    uv run python benchmark/bootstrap.py \
        --system sc=benchmark/work/poj104/test/results-sc/Benchmark-Result.csv \
        --system fv=benchmark/work/poj104/test/results-fv/Benchmark-Result.csv \
        --system jplag=benchmark/work/poj104/test/results-jplag/Benchmark-Result.csv \
        --threshold sc=9.375 --threshold fv=10.472 --threshold jplag=50 \
        --iterations 10000 --seed 20260720 --output metrics-bootstrap.json

The cluster of a pair is the problem id parsed from ``Program1`` (the materialized name
``<corpus>__<problem>__<submission>.cpp``); pass ``--cluster-regex`` for other layouts.
"""

from __future__ import annotations

import argparse
import csv
import json
import random
import re
from bisect import bisect_left
from pathlib import Path


def roc_auc(scores: list[float], labels: list[bool]) -> float:
    positives = sorted(s for s, l in zip(scores, labels) if l)
    negatives = sorted(s for s, l in zip(scores, labels) if not l)
    if not positives or not negatives:
        return float("nan")
    total = 0.0
    for score in positives:
        below = bisect_left(negatives, score)
        equal = 0
        index = below
        while index < len(negatives) and negatives[index] == score:
            equal += 1
            index += 1
        total += below + 0.5 * equal
    return total / (len(positives) * len(negatives))


def average_precision(scores: list[float], labels: list[bool]) -> float:
    """Average precision with tied scores treated as one threshold group."""
    order = sorted(zip(scores, labels), key=lambda item: -item[0])
    total_positive = sum(1 for _, label in order if label)
    if total_positive == 0:
        return float("nan")
    seen = 0
    hits = 0
    result = 0.0
    index = 0
    while index < len(order):
        score = order[index][0]
        group_hits = 0
        group_size = 0
        while index < len(order) and order[index][0] == score:
            group_size += 1
            group_hits += 1 if order[index][1] else 0
            index += 1
        seen += group_size
        hits += group_hits
        if group_hits:
            result += (hits / seen) * group_hits
    return result / total_positive


def f1_at(scores: list[float], labels: list[bool], threshold: float) -> tuple[float, float, float]:
    tp = fp = fn = 0
    for score, label in zip(scores, labels):
        predicted = score >= threshold
        if predicted and label:
            tp += 1
        elif predicted and not label:
            fp += 1
        elif not predicted and label:
            fn += 1
    precision = tp / (tp + fp) if tp + fp else 0.0
    recall = tp / (tp + fn) if tp + fn else 0.0
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return precision, recall, f1


def read_results(path: Path, cluster_regex: re.Pattern[str]) -> dict[str, tuple[str, float, bool]]:
    rows: dict[str, tuple[str, float, bool]] = {}
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            match = cluster_regex.search(row["Program1"])
            if not match:
                raise SystemExit(f"cannot parse cluster from {row['Program1']!r} in {path}")
            rows[row["PairId"]] = (match.group(1), float(row["Score"]), row["Label"] == "1")
    return rows


def percentile(values: list[float], q: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return float("nan")
    position = (len(ordered) - 1) * q
    low = int(position)
    high = min(low + 1, len(ordered) - 1)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def metrics_for(scores: list[float], labels: list[bool], threshold: float | None) -> dict[str, float]:
    result = {"roc_auc": roc_auc(scores, labels), "pr_auc": average_precision(scores, labels)}
    if threshold is not None:
        precision, recall, f1 = f1_at(scores, labels, threshold)
        result.update({"precision": precision, "recall": recall, "f1": f1})
    return result


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--system", action="append", required=True, help="name=path/to/Benchmark-Result.csv")
    parser.add_argument("--threshold", action="append", default=[], help="name=fixed threshold for precision/recall/F1")
    parser.add_argument("--iterations", type=int, default=10000)
    parser.add_argument("--seed", type=int, default=20260720)
    parser.add_argument("--cluster-regex", default=r"^[^_]+__([^_]+)__")
    parser.add_argument("--output", default=None)
    args = parser.parse_args(argv)

    cluster_regex = re.compile(args.cluster_regex)
    systems: dict[str, dict[str, tuple[str, float, bool]]] = {}
    for spec in args.system:
        name, _, path = spec.partition("=")
        systems[name] = read_results(Path(path), cluster_regex)
    thresholds: dict[str, float] = {}
    for spec in args.threshold:
        name, _, value = spec.partition("=")
        thresholds[name] = float(value)

    names = list(systems)
    pair_ids = sorted(systems[names[0]])
    for name in names[1:]:
        if sorted(systems[name]) != pair_ids:
            raise SystemExit(f"system {name} does not cover the same pair ids as {names[0]}")
    clusters: dict[str, list[str]] = {}
    for pair_id in pair_ids:
        clusters.setdefault(systems[names[0]][pair_id][0], []).append(pair_id)
    cluster_ids = sorted(clusters)

    def evaluate(selected: list[str]) -> dict[str, dict[str, float]]:
        out: dict[str, dict[str, float]] = {}
        for name in names:
            rows = systems[name]
            scores = [rows[p][1] for c in selected for p in clusters[c]]
            labels = [rows[p][2] for c in selected for p in clusters[c]]
            out[name] = metrics_for(scores, labels, thresholds.get(name))
        return out

    point = evaluate(cluster_ids)
    rng = random.Random(args.seed)
    samples: dict[str, dict[str, list[float]]] = {n: {} for n in names}
    differences: dict[str, dict[str, list[float]]] = {}
    for _ in range(args.iterations):
        drawn = [cluster_ids[rng.randrange(len(cluster_ids))] for _ in cluster_ids]
        result = evaluate(drawn)
        for name in names:
            for metric, value in result[name].items():
                samples[name].setdefault(metric, []).append(value)
        for i, a in enumerate(names):
            for b in names[i + 1:]:
                key = f"{a}-{b}"
                for metric in result[a]:
                    if metric in result[b]:
                        differences.setdefault(key, {}).setdefault(metric, []).append(result[a][metric] - result[b][metric])

    report = {
        "iterations": args.iterations,
        "seed": args.seed,
        "clusters": len(cluster_ids),
        "pairs": len(pair_ids),
        "thresholds": thresholds,
        "systems": {},
        "differences": {},
    }
    for name in names:
        report["systems"][name] = {
            metric: {
                "point": point[name][metric],
                "ci95": [percentile(values, 0.025), percentile(values, 0.975)],
            }
            for metric, values in samples[name].items()
        }
    for key, metrics in differences.items():
        a, b = key.split("-")
        report["differences"][key] = {
            metric: {
                "point": point[a][metric] - point[b][metric],
                "ci95": [percentile(values, 0.025), percentile(values, 0.975)],
                "ci_excludes_zero": percentile(values, 0.025) > 0 or percentile(values, 0.975) < 0,
            }
            for metric, values in metrics.items()
        }

    text = json.dumps(report, indent=2)
    if args.output:
        Path(args.output).write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
