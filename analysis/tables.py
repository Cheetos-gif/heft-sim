#!/usr/bin/env python3
"""analysis/tables.py

Summary statistics: mean/median/min/max predicted-vs-measured error% per
scheduler, from out/results.csv (benchmark_suite.cpp's output). This is
the number the project's research claim lives or dies on -- whether
cheft's error% distribution is actually tighter than eheft's, and (via
pheft, the comm-model/contention-isolation control -- src/sched/
pheft.hpp) *why*: eheft->pheft isolates the comm-cost-model effect,
pheft->cheft isolates contention-awareness alone. naive isn't a fair
comparison point the same way: it's calibrated too, via the same
CalibrationProfile, but its own cost model never claimed to predict
contention in the first place.

Usage: python3 analysis/tables.py [path/to/results.csv]
"""

from __future__ import annotations

import statistics
import sys

from common import group_by_scheduler, group_by_shape_and_scheduler, load_results


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

    by_shape_sched = group_by_shape_and_scheduler(rows)
    shapes = sorted({r.shape for r in rows})
    schedulers = sorted({r.scheduler for r in rows})

    print()
    print("Per-shape mean |error|% (averaged across seed replicates within each shape):")
    header = f"{'shape':<16}" + "".join(f"{s:>12}" for s in schedulers)
    print(header)
    print("-" * len(header))
    for shape in shapes:
        cells = []
        for sched in schedulers:
            group = by_shape_sched.get((shape, sched), [])
            mean_abs = statistics.mean(abs(r.error_pct) for r in group) if group else float("nan")
            cells.append(f"{mean_abs:>12.1f}")
        print(f"{shape:<16}" + "".join(cells))

    # Head-to-head: on how many shapes does cheft's mean |error| beat
    # eheft's? (Not asserted anywhere as a pass/fail -- see
    # tests/fixtures/cheft.cpp's header comment on why that would be
    # flaky; this is purely descriptive.)
    if "cheft" in schedulers and "eheft" in schedulers:
        cheft_wins = 0
        eheft_wins = 0
        for shape in shapes:
            cheft_group = by_shape_sched.get((shape, "cheft"), [])
            eheft_group = by_shape_sched.get((shape, "eheft"), [])
            if not cheft_group or not eheft_group:
                continue
            cheft_mean = statistics.mean(abs(r.error_pct) for r in cheft_group)
            eheft_mean = statistics.mean(abs(r.error_pct) for r in eheft_group)
            if cheft_mean < eheft_mean:
                cheft_wins += 1
            elif eheft_mean < cheft_mean:
                eheft_wins += 1
        print()
        print(f"cheft vs eheft, mean |error|% lower on: cheft {cheft_wins}/{len(shapes)} shapes, "
              f"eheft {eheft_wins}/{len(shapes)} shapes")

    # pheft isolates the two things eheft->cheft conflates (see
    # src/sched/pheft.hpp): eheft->pheft holds contention-awareness at
    # "off" and changes only the comm-cost model (flat mean ->
    # per-processor-pair); pheft->cheft then holds the comm model
    # constant and changes only contention-awareness ("off" -> "on").
    # Reporting both gaps separately, instead of only eheft-vs-cheft,
    # shows which of the two actually accounts for cheft's edge.
    if "pheft" in schedulers:
        def wins(a: str, b: str) -> tuple[int, int]:
            a_wins = b_wins = 0
            for shape in shapes:
                a_group = by_shape_sched.get((shape, a), [])
                b_group = by_shape_sched.get((shape, b), [])
                if not a_group or not b_group:
                    continue
                a_mean = statistics.mean(abs(r.error_pct) for r in a_group)
                b_mean = statistics.mean(abs(r.error_pct) for r in b_group)
                if a_mean < b_mean:
                    a_wins += 1
                elif b_mean < a_mean:
                    b_wins += 1
            return a_wins, b_wins

        print()
        if "eheft" in schedulers:
            p_wins, e_wins = wins("pheft", "eheft")
            print(f"[comm-model isolation] pheft vs eheft, mean |error|% lower on: "
                  f"pheft {p_wins}/{len(shapes)} shapes, eheft {e_wins}/{len(shapes)} shapes "
                  f"(contention-awareness held constant at 'off'; only the comm-cost model differs)")
        if "cheft" in schedulers:
            c_wins, p_wins2 = wins("cheft", "pheft")
            print(f"[contention isolation] cheft vs pheft, mean |error|% lower on: "
                  f"cheft {c_wins}/{len(shapes)} shapes, pheft {p_wins2}/{len(shapes)} shapes "
                  f"(comm-cost model held constant at per-pair; only contention-awareness differs)")

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
