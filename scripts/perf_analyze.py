#!/usr/bin/env python3
"""Build comparison tables and plots from score_all perf CSV rows."""

import argparse
import csv
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt


NUMERIC_FIELDS = [
    "samples",
    "match_ms_avg",
    "score_all_ms_avg",
    "score_all_ratio_pct",
    "score_all_throughput_mchecks_s_avg",
    "score_all_candidates_total",
    "score_all_evaluated_candidates_total",
    "score_all_cell_checks_total",
    "score_all_evaluated_cell_checks_total",
]


def to_float(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def read_rows(path):
    with Path(path).open(newline="") as f:
        return list(csv.DictReader(f))


def group_rows(rows):
    groups = defaultdict(list)
    for row in rows:
        key = row.get("label") or row.get("score_all_backend_requested")
        groups[key].append(row)
    return groups


def summarize(groups):
    summaries = []
    for label, rows in groups.items():
        summary = {
            "label": label,
            "runs": len(rows),
            "step_order": min(
                [to_float(r.get("step_order")) for r in rows if r.get("step_order")]
                or [0.0]
            ),
            "backend_requested": rows[-1].get("score_all_backend_requested", ""),
            "backend_active": rows[-1].get("score_all_backend_active", ""),
        }
        for field in NUMERIC_FIELDS:
            summary[field] = sum(to_float(r.get(field)) for r in rows) / len(rows)
        input_checks = summary["score_all_cell_checks_total"]
        eval_checks = summary["score_all_evaluated_cell_checks_total"]
        summary["evaluated_cell_check_ratio_pct"] = (
            100.0 * eval_checks / input_checks if input_checks > 0.0 else 0.0
        )
        summaries.append(summary)
    summaries.sort(key=lambda item: (item["step_order"], item["label"]))
    return summaries


def add_deltas(summaries):
    if not summaries:
        return
    cpu_ms = summaries[0]["score_all_ms_avg"]
    prev = None
    for summary in summaries:
        score_ms = summary["score_all_ms_avg"]
        throughput = summary["score_all_throughput_mchecks_s_avg"]
        summary["cpu_speedup_x"] = cpu_ms / score_ms if score_ms > 0.0 else 0.0
        if prev is None:
            summary["prev_score_all_ms_change_pct"] = 0.0
            summary["prev_throughput_change_pct"] = 0.0
        else:
            prev_ms = prev["score_all_ms_avg"]
            prev_thr = prev["score_all_throughput_mchecks_s_avg"]
            summary["prev_score_all_ms_change_pct"] = (
                100.0 * (score_ms - prev_ms) / prev_ms if prev_ms > 0.0 else 0.0
            )
            summary["prev_throughput_change_pct"] = (
                100.0 * (throughput - prev_thr) / prev_thr
                if prev_thr > 0.0
                else 0.0
            )
        prev = summary


def write_summary_csv(path, summaries):
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "label",
        "runs",
        "backend_requested",
        "backend_active",
        "samples",
        "match_ms_avg",
        "score_all_ms_avg",
        "score_all_throughput_mchecks_s_avg",
        "cpu_speedup_x",
        "prev_score_all_ms_change_pct",
        "prev_throughput_change_pct",
        "score_all_candidates_total",
        "score_all_evaluated_candidates_total",
        "score_all_cell_checks_total",
        "score_all_evaluated_cell_checks_total",
        "evaluated_cell_check_ratio_pct",
    ]
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows({field: row.get(field, "") for field in fields}
                         for row in summaries)


def write_markdown(path, summaries):
    path.parent.mkdir(parents=True, exist_ok=True)
    headers = [
        "step",
        "score_all_ms",
        "throughput_Mchecks_s",
        "cpu_speedup_x",
        "prev_ms_change_pct",
        "eval_check_ratio_pct",
    ]
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]
    for row in summaries:
        lines.append(
            "| {label} | {score_all_ms_avg:.3f} | "
            "{score_all_throughput_mchecks_s_avg:.3f} | "
            "{cpu_speedup_x:.3f} | {prev_score_all_ms_change_pct:.2f} | "
            "{evaluated_cell_check_ratio_pct:.2f} |".format(**row)
        )
    path.write_text("\n".join(lines) + "\n")


def find_first(summaries, predicate):
    for row in summaries:
        if predicate(row):
            return row
    return None


def add_block_ablation_deltas(summaries):
    block_base = find_first(
        summaries,
        lambda row: "test3" in row["label"]
        or row["backend_requested"] == "gpu_block_per_candidate",
    )
    test4_base = find_first(
        summaries,
        lambda row: (
            "test4" in row["label"]
            or row["backend_requested"] == "gpu_block_cached_map_shared"
        )
        and "pinned" not in row["label"]
        and row["backend_requested"] != "gpu_block_cached_map_shared_pinned",
    )
    if block_base is None:
        return []
    block_ms = block_base["score_all_ms_avg"]
    test4_ms = test4_base["score_all_ms_avg"] if test4_base else 0.0
    rows = []
    for row in summaries:
        label = row["label"]
        backend = row["backend_requested"]
        is_block_family = (
            "test3" in label
            or "test4" in label
            or "pinned" in label
            or backend
            in {
                "gpu_block_per_candidate",
                "gpu_block_cached_map_shared",
                "gpu_block_cached_map_shared_pinned",
            }
        )
        if not is_block_family:
            continue
        score_ms = row["score_all_ms_avg"]
        item = dict(row)
        item["vs_test3_speedup_x"] = block_ms / score_ms if score_ms > 0.0 else 0.0
        item["vs_test3_ms_change_pct"] = (
            100.0 * (score_ms - block_ms) / block_ms if block_ms > 0.0 else 0.0
        )
        item["vs_test4_ms_change_pct"] = (
            100.0 * (score_ms - test4_ms) / test4_ms if test4_ms > 0.0 else 0.0
        )
        rows.append(item)
    return rows


def write_block_ablation_csv(path, rows):
    if not rows:
        return
    fields = [
        "label",
        "backend_requested",
        "score_all_ms_avg",
        "score_all_throughput_mchecks_s_avg",
        "vs_test3_speedup_x",
        "vs_test3_ms_change_pct",
        "vs_test4_ms_change_pct",
        "evaluated_cell_check_ratio_pct",
    ]
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows({field: row.get(field, "") for field in fields}
                         for row in rows)


def write_block_ablation_markdown(path, rows):
    if not rows:
        return
    headers = [
        "step",
        "score_all_ms",
        "throughput_Mchecks_s",
        "vs_TEST3_speedup_x",
        "vs_TEST3_ms_change_pct",
        "vs_TEST4_ms_change_pct",
    ]
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]
    for row in rows:
        lines.append(
            "| {label} | {score_all_ms_avg:.3f} | "
            "{score_all_throughput_mchecks_s_avg:.3f} | "
            "{vs_test3_speedup_x:.3f} | {vs_test3_ms_change_pct:.2f} | "
            "{vs_test4_ms_change_pct:.2f} |".format(**row)
        )
    path.write_text("\n".join(lines) + "\n")


def save_plot(path, summaries, field, ylabel):
    labels = [row["label"] for row in summaries]
    values = [row[field] for row in summaries]
    plt.figure(figsize=(max(8, len(labels) * 1.4), 4.8))
    plt.bar(labels, values)
    plt.ylabel(ylabel)
    plt.xticks(rotation=25, ha="right")
    plt.tight_layout()
    plt.savefig(path)
    plt.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", default="results/perf_runs.csv")
    parser.add_argument("--out-dir", default="results")
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    summaries = summarize(group_rows(read_rows(args.csv)))
    add_deltas(summaries)
    block_rows = add_block_ablation_deltas(summaries)
    write_summary_csv(out_dir / "perf_analysis_summary.csv", summaries)
    write_markdown(out_dir / "perf_analysis_table.md", summaries)
    write_block_ablation_csv(out_dir / "block_ablation_summary.csv", block_rows)
    write_block_ablation_markdown(out_dir / "block_ablation_table.md",
                                  block_rows)
    save_plot(out_dir / "score_all_ms_avg.png", summaries,
              "score_all_ms_avg", "score_all ms avg")
    save_plot(out_dir / "throughput_avg.png", summaries,
              "score_all_throughput_mchecks_s_avg", "Mchecks/s avg")
    save_plot(out_dir / "cpu_speedup.png", summaries,
              "cpu_speedup_x", "CPU speedup x")
    print(f"wrote analysis to {out_dir}")


if __name__ == "__main__":
    main()
