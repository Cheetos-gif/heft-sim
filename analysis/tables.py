#!/usr/bin/env python3
"""analysis/tables.py

Summary statistics: mean/median/min/max predicted-vs-measured error% per
scheduler, from out/results.csv (benchmark_suite.cpp's output). This is
the number the project's research claim lives or dies on -- whether
cheft's error% distribution is actually tighter than eheft's (naive
isn't a fair comparison point the same way: it's calibrated too, via the
same CalibrationProfile, but its own cost model never claimed to predict
contention in the first place).

Usage: python3 analysis/tables.py [path/to/results.csv]
"""

from __future__ import annotations

import statistics
import sys

from common import group_by_scheduler, load_results


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "out/results.csv"
    try:
        rows = load_results(path)
    except FileNotFoundError:
        print(f"{path} does not exist -- run benchmark_suite first")
        return

    if not rows:
        print(f"no rows in {path}")
        return

    by_sched = group_by_scheduler(rows)

    print("Per-scheduler summary (predicted-vs-measured error%, |error| = accuracy regardless of sign):")
    print(f"{'scheduler':<10} {'n':>4} {'mean err%':>10} {'median err%':>12} {'min err%':>10} {'max err%':>10} "
          f"{'mean |err|%':>12}")
    print("-" * 10, "-" * 4, "-" * 10, "-" * 12, "-" * 10, "-" * 10, "-" * 12)
    for sched in sorted(by_sched):
        errs = [r.error_pct for r in by_sched[sched]]
        abs_errs = [abs(e) for e in errs]
        print(
            f"{sched:<10} {len(errs):>4} {statistics.mean(errs):>10.1f} "
            f"{statistics.median(errs):>12.1f} {min(errs):>10.1f} {max(errs):>10.1f} "
            f"{statistics.mean(abs_errs):>12.1f}"
        )

    print()
    print("Per-run detail:")
    print(f"{'dag_id':<16} {'scheduler':<10} {'tasks':>6} {'predicted(s)':>13} {'measured(s)':>12} {'error%':>8}")
    print("-" * 16, "-" * 10, "-" * 6, "-" * 13, "-" * 12, "-" * 8)
    for r in rows:
        print(
            f"{r.dag_id:<16} {r.scheduler:<10} {r.num_tasks:>6} {r.predicted_sec:>13.4f} "
            f"{r.measured_sec:>12.4f} {r.error_pct:>7.1f}%"
        )


if __name__ == "__main__":
    main()
