#!/usr/bin/env python3
"""Extract perf_summary_json lines from ROS logs and append them to CSV."""

import argparse
import csv
import datetime as dt
import json
import re
from pathlib import Path


FIELDS = [
    "timestamp",
    "label",
    "step_order",
    "score_all_backend_requested",
    "score_all_backend_active",
    "score_all_cuda_build",
    "samples",
    "match_ms_avg",
    "score_all_ms_avg",
    "score_all_ratio_pct",
    "score_all_throughput_mchecks_s_avg",
    "score_avg",
    "match_ms_sum",
    "score_all_ms_sum",
    "score_all_calls_total",
    "score_all_candidates_total",
    "score_all_evaluated_candidates_total",
    "score_all_scan_points_total",
    "score_all_cell_checks_total",
    "score_all_evaluated_cell_checks_total",
    "last_match_ms",
    "last_score_all_ms",
    "last_score_all_ratio_pct",
    "last_score_all_throughput_mchecks_s",
    "last_score",
    "last_score_all_calls",
    "last_score_all_candidates",
    "last_score_all_evaluated_candidates",
    "last_score_all_cell_checks",
    "last_score_all_evaluated_cell_checks",
    "source",
]


def flatten_summary(summary, label, step_order, source):
    last = summary.get("last", {})
    row = {field: "" for field in FIELDS}
    row.update(
        {
            "timestamp": dt.datetime.now().isoformat(timespec="seconds"),
            "label": label or summary.get("score_all_backend_requested", ""),
            "step_order": step_order,
            "source": source,
        }
    )
    for field in FIELDS:
        if field.startswith("last_"):
            row[field] = last.get(field.removeprefix("last_"), "")
        elif field in summary:
            row[field] = summary[field]
    return row


def extract_summaries(path):
    text = Path(path).read_text(errors="replace")
    for match in re.finditer(r"perf_summary_json=(\{.*\})", text):
        yield json.loads(match.group(1))


def append_rows(csv_path, rows):
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    exists = csv_path.exists() and csv_path.stat().st_size > 0
    with csv_path.open("a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        if not exists:
            writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", help="ROS log files to scan")
    parser.add_argument("--csv", default="results/perf_runs.csv")
    parser.add_argument("--label", default="")
    parser.add_argument("--step-order", default="")
    args = parser.parse_args()

    rows = []
    for input_path in args.inputs:
      for summary in extract_summaries(input_path):
          rows.append(
              flatten_summary(summary, args.label, args.step_order, input_path)
          )
    append_rows(Path(args.csv), rows)
    print(f"appended {len(rows)} rows to {args.csv}")


if __name__ == "__main__":
    main()
