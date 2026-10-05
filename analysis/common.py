"""analysis/common.py

Shared CSV loading for analysis/plots.py and analysis/tables.py. Reads
out/results.csv (src/exec/results_log.hpp's schema, written by
benchmark_suite.cpp): one row per (dag, scheduler) run.

No third-party dependencies -- this environment has no pip/network
access to install matplotlib/pandas, so every analysis script here uses
only the standard library.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Result:
    dag_id: str
    num_tasks: int
    num_processors: int
    scheduler: str
    predicted_sec: float
    measured_sec: float
    error_pct: float
    throughput: float


def load_results(path: str | Path) -> list[Result]:
    rows: list[Result] = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(
                Result(
                    dag_id=r["dag_id"],
                    num_tasks=int(r["num_tasks"]),
                    num_processors=int(r["num_processors"]),
                    scheduler=r["scheduler"],
                    predicted_sec=float(r["predicted_sec"]),
                    measured_sec=float(r["measured_sec"]),
                    error_pct=float(r["error_pct"]),
                    throughput=float(r["throughput"]),
                )
            )
    return rows


def group_by_scheduler(rows: list[Result]) -> dict[str, list[Result]]:
    out: dict[str, list[Result]] = {}
    for r in rows:
        out.setdefault(r.scheduler, []).append(r)
    return out
