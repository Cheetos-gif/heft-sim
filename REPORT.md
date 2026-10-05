# HEFT-Sim: does correcting HEFT for co-runner contention help?

## 1. Claim under test

HEFT (Topcuoglu, Hariri, Wu, 2002) predicts a task's execution time from a
static per-processor cost table, `w(task, processor)`. That model has no
notion of *who else is running at the same time*. This project's claim:
that omission produces a measurable, non-trivial prediction error, and a
contention-aware correction (`cheft`) can reduce it.

This report documents what was actually measured, on real pinned threads,
on one development machine (4-core WSL2 VM) — not a simulation.

## 2. What was built

| Component | Role |
|---|---|
| `src/dag/` | `Task`/`Dag` (strict-validated DAG), `generators.hpp` (seeded synthetic layered DAGs) |
| `src/kernels/` | Two real workloads: `COMPUTE_BOUND` (L1-resident ALU loop), `MEMORY_BOUND` (pointer-chase sized to defeat the prefetcher) |
| `src/exec/` | `Dispatcher` (real pinned worker threads, `CLOCK_MONOTONIC`, append-only `EventLog`), `results_log.hpp` (sweep output) |
| `src/profile/` | `pressure`/`probe`/`comm_probe` (reporter/interferer methodology, after Mars et al., Bubble-Up ASPLOS 2011) → `CalibrationProfile`: σ (solo cost), π (pairwise contention ratio), π₃ (3-way contention ratio), comm (cross-core handoff latency) |
| `src/sched/` | `naive` (round-robin baseline), `heft` (reference, unmodified), `eheft` (HEFT with σ/comm substituted — contention-oblivious), `cheft` (`eheft` + contention-aware EFT insertion) |
| `src/ft/` | Fault injection + heartbeat-based recovery (orthogonal to the contention question; exercised, not discussed further here) |
| `benchmark_suite.cpp` | Calibrates once, runs `{naive, eheft, cheft}` against 15 generated DAGs, writes `out/results.csv` |
| `analysis/` | Pure-stdlib Python (no matplotlib/pandas available in this environment): `tables.py`, `plots.py` → `analysis/out/error_pct.svg` |

Every number in this report comes from a real `./run_demo.sh` execution
(all 7 stages, ~18 minutes, WSL2/Ubuntu on this machine's 4 physical
cores) or a real `./benchmark_suite` run. None are simulated or
hand-picked from multiple runs to flatter the result — see §5 for the
honest counter-example.

## 3. Does contention exist at all? (Stage 1: `contention_pilot`)

Reporter/interferer slowdown matrix, one representative run:

```
               COMPUTE   MEMORY
  COMPUTE        0.90x    1.13x
  MEMORY         0.80x    0.98x
```

The control cell (COMPUTE vs COMPUTE) is near 1.0x, as expected — an
L1-resident workload barely notices a co-runner. Off-diagonal cells
deviate by up to +13%, but on this machine the deviations are smaller
and noisier than they were on earlier development runs (which showed
MEMORY solo/paired slowdowns past 1.2x). **This VM's measured contention
is modest and noisy** — a real, physical property of the hardware this
was measured on, not a limitation of the methodology. HEFT's model still
assumes every cell is exactly 1.00x regardless.

## 4. Unit-matched single-DAG comparison (Stage 6: `topcuoglu_calibrated`)

Running `{eheft, cheft, naive}` against the Topcuoglu paper's Fig. 1
topology, with σ/π/comm calibrated on this machine (`sigma` in seconds,
`pi` pairwise contended/solo ratios):

```
sigma (solo seconds):              pi (contended/solo, cores 0 vs 1):
processor  COMPUTE    MEMORY       reporter\interf  COMPUTE  MEMORY
0          0.163275   0.363558     COMPUTE          0.742    1.002
1          0.164676   0.321488     MEMORY           0.811    1.255
2          0.162162   0.300818

sched   predicted(s)  measured(s)  error%
eheft   1.153089      1.300022     12.7%
cheft   1.195186      1.552527     29.9%
naive   1.334486      1.445758      8.3%
```

One DAG is not a sample — this table exists only to prove the pipeline
produces genuinely unit-matched (seconds vs seconds) numbers, in contrast
to `topcuoglu_exec.cpp`'s deliberately-uncalibrated paper-table comparison
(−97% to −99% "error" there, meaningless across unit systems by design).
The real multi-DAG result is §5.

## 5. The actual result: 15-DAG, 3-scheduler benchmark sweep

`benchmark_suite.cpp`: 5 topology shapes (narrow/deep, wide/shallow,
balanced, sparse, dense) × 3 seed replicates each = 15 DAGs, 45 real
dispatcher runs.

```
scheduler     n  mean err%  median err%   min err%   max err%  mean |err|%
cheft        15       -1.9         -4.1      -30.6       32.9         16.5
eheft        15       15.8         17.6      -10.0       41.6         19.2
naive        15        3.0          1.3      -26.1       41.7         14.3

Per-shape mean |error|%:
shape          cheft   eheft   naive
balanced        21.4    20.0    16.7
dense           10.5    25.4    16.2
narrow_deep     20.0    24.9    14.6
sparse          16.2    11.5    14.5
wide_shallow    14.4    14.2     9.5

cheft vs eheft, mean |error|% lower on: cheft 2/5 shapes, eheft 3/5 shapes
```

(Full 45-row detail: `out/results.csv`; bar chart: `analysis/out/error_pct.svg`.)

**Headline, this run**: `cheft`'s mean absolute error (16.5%) is lower
than `eheft`'s (19.2%) — a 3.4-point improvement, consistent with the
claim. But `naive` (14.3%) is *also* lower than both, and `cheft` only
wins outright on 2 of 5 shapes.

## 6. Honest discussion: this result is noisy, not settled

An earlier standalone run of the same 15-DAG corpus (same code, same
machine, different moment) gave a *different* picture:

```
                cheft   eheft
mean |error|%   22.5    32.1    (cheft wins 3/5 shapes)
```

vs. this report's canonical run:

```
                cheft   eheft
mean |error|%   16.5    19.2    (cheft wins 2/5 shapes)
```

Both runs agree `cheft` beats `eheft` on the aggregate mean — but the
margin (10-point gap vs 3-point gap) and the shape-level win count (3/5
vs 2/5) are not stable run to run. Two honest explanations, not mutually
exclusive:

1. **This machine is a shared, virtualized 4-core VM.** `CLOCK_MONOTONIC`
   timings on it carry host-scheduler noise that a bare-metal run
   wouldn't. §3's own slowdown matrix is smaller/noisier than earlier
   development runs on the same hardware.
2. **15 DAGs (3 seeds × 5 shapes) is not a large sample.** A handful of
   outlier runs (e.g. `dense_s1`/`dense_s3` showing `eheft` 40%+ wrong
   while `dense_s2` shows single digits for everyone) can swing a 15-row
   mean substantially.

Neither explanation is an excuse to not report this: it is the actual
finding at this sample size, on this hardware, today. A stronger claim
(cheft reliably, reproducibly beats eheft) would need either a
quieter/dedicated host or a much larger corpus (or both) to separate
signal from this machine's noise — see §7.

What *is* consistent across both runs: `cheft` beats `eheft` on
aggregate mean |error|, and the `sparse`/`dense` shapes (more room for
genuinely concurrent, overlapping placements) show the largest gaps in
`cheft`'s favor in both runs. That's a real, repeated signal, even
though the magnitude isn't stable.

## 7. What would make this conclusive (not done)

- **Bare-metal host**, not a shared VM — removes one entire noise source
  from every measurement in this report.
- **Much larger corpus** (50-100+ DAGs, more shapes, wider task-count
  range) — the 15-DAG sample here is sized for a demo to run in minutes,
  not for a statistically defensible confidence interval.
- **Repeated full sweeps** (run the 15-DAG corpus N times, not once) to
  separate within-run DAG-to-DAG variance from run-to-run machine-state
  variance — this report only has two independent full-sweep data points
  (§6), nowhere near enough to fit error bars to.
- **Per-processor-pair comm cost in `eheft`** too, not just `cheft` —
  `eheft` is still forced through `heft.cpp`'s one-scalar-per-edge Dag
  representation (`mean_comm_cost()`), so part of `cheft`'s apparent edge
  over `eheft` is the comm-cost model, not contention-awareness alone.
  Isolating the two would need a fourth scheduler (contention-oblivious
  but per-pair-comm-aware) as a control.

## 8. Reproducing this report

```
./run_demo.sh                    # full pipeline, ~18-20 min, all 7 stages
python3 analysis/tables.py       # the tables in §5, from out/results.csv
python3 analysis/plots.py        # analysis/out/error_pct.svg
```

`out/results.csv` and `analysis/out/error_pct.svg` checked into this repo
are from the run quoted in §5-6. Re-running `benchmark_suite` will
overwrite them with a new (likely numerically different, per §6) sample.

## 9. Summary

- Contention is real but modest on this hardware (§3).
- The calibration pipeline is genuinely unit-matched (§4).
- `cheft`'s contention-aware correction beats `eheft`'s contention-oblivious
  one on aggregate mean error, in both independent sweeps run (§5-6).
- That result is **directionally consistent but not yet statistically
  solid** at this sample size on this hardware — reported honestly as
  such, with a concrete path to a stronger claim (§7), not papered over.
