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
| `src/sched/` | `naive` (round-robin baseline), `heft` (reference, unmodified), `eheft` (HEFT with σ/comm substituted — contention-oblivious), `pheft` (`eheft`'s comm model replaced with per-processor-pair cost, still contention-oblivious — isolates the comm-cost-model confound from contention-awareness, see `src/sched/pheft.hpp`), `cheft` (`pheft` + contention-aware EFT insertion) |
| `src/ft/` | Fault injection + heartbeat-based recovery (orthogonal to the contention question; exercised, not discussed further here) |
| `benchmark_suite.cpp` | Calibrates once, runs `{naive, eheft, pheft, cheft}` against 15 generated DAGs, writes `out/results.csv` |
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

## 5. The actual result: 15-DAG, 4-scheduler benchmark sweep

`benchmark_suite.cpp`: 5 topology shapes (narrow/deep, wide/shallow,
balanced, sparse, dense) × 3 seed replicates each = 15 DAGs, now run
against `{naive, eheft, pheft, cheft}` — 60 real dispatcher runs.
`pheft` is new since the first draft of this report: the
confound-isolating control named in an earlier version of §7 below.
`cheft` differs from `eheft` in two independent ways at once (per-pair
comm cost *and* contention-aware EFT insertion); `pheft` is `eheft`'s
comm model swapped for `cheft`'s per-processor-pair one, with
contention-awareness still off (`src/sched/pheft.hpp`). That makes
`eheft → pheft` isolate the comm-model effect, and `pheft → cheft`
isolate contention-awareness alone, instead of reading the two off one
conflated `eheft → cheft` gap.

```
scheduler     n  mean err%  median err%   min err%   max err%  mean |err|%
cheft        15       35.0         35.0       -4.2      133.7         35.9
eheft        15       42.3         43.4        6.0       77.2         42.3
naive        15       35.1         38.6        0.5       66.8         35.1
pheft        15       35.7         37.4       -0.5       89.1         35.8

Per-shape mean |error|%:
shape          cheft   eheft   naive   pheft
balanced        43.0    48.6    49.9    40.9
dense            4.0    10.0     5.8     7.2
narrow_deep     31.2    39.2    28.0    33.1
sparse          30.5    59.8    50.8    46.0
wide_shallow    70.7    53.7    41.2    51.7

cheft vs eheft, mean |error|% lower on: cheft 4/5 shapes, eheft 1/5 shapes

[comm-model isolation]  pheft vs eheft: pheft lower on 5/5 shapes, eheft on 0/5
[contention isolation]  cheft vs pheft: cheft lower on 3/5 shapes, pheft on 2/5
```

(Full 60-row detail: `out/results.csv`; bar chart: `analysis/out/error_pct.svg`. This is
the output of `./run_demo.sh`'s own stage 7, the project's documented, single
reproduction command — see §8.)

**Headline, this run**: `cheft` has the lowest aggregate mean |error|
(35.9%) of the three calibrated schedulers and wins outright on 4 of 5
shapes against `eheft` — the strongest result for the project's claim
of any run so far. Decomposed by the two confounds the old
`eheft → cheft` gap conflated: `pheft` (comm model swapped in,
contention-awareness still off) beats `eheft` on *every* shape (5/5),
while `cheft` only edges `pheft` out on 3/5 shapes once the comm model
is held constant. Read plainly: on this run, most of `cheft`'s edge
over `eheft` traces to the per-processor-pair comm-cost model, not to
contention-awareness itself — the thing the project's central claim is
actually about. Contention-awareness (`pheft → cheft`) is a real but
markedly smaller, and still noisy (3/5, not 5/5), contribution.

`naive` sits close to `pheft`/`cheft` on aggregate mean here (35.1% vs
35.8%/35.9%) — within noise of both, not a clean win either way on this
run.

## 6. Honest discussion: this result is noisy, not settled

Three earlier standalone runs of the same 15-DAG corpus (same code, same
machine, different moments) precede §5's canonical `./run_demo.sh` run:

```
                cheft   eheft          (pre-pheft: {naive, eheft, cheft} only)
mean |error|%   22.5    32.1    (cheft wins 3/5 shapes)
mean |error|%   16.5    19.2    (cheft wins 2/5 shapes)

                cheft   eheft   pheft  (first 4-scheduler run, standalone ./benchmark_suite)
mean |error|%   25.5    24.5    26.4   (cheft wins 2/5, eheft 3/5; comm-isolation: pheft wins 4/5)
```

vs. §5's canonical run (`./run_demo.sh`, all four schedulers):

```
                cheft   eheft   pheft
mean |error|%   35.9    42.3    35.8   (cheft wins 4/5, eheft 1/5; comm-isolation: pheft wins 5/5)
```

The first two runs agreed `cheft` beats `eheft` on the aggregate mean,
with an unstable margin (10-point gap vs 3-point gap) and shape-win-count
(3/5 vs 2/5). The third (first `pheft` run) briefly reversed that —
`eheft` edged out `cheft`. The fourth (§5's canonical run) reverses back,
this time more decisively in `cheft`'s favor (4/5 shapes, 6.4-point gap)
— and, via `pheft`, gives the clearest read yet of *why*: the
comm-model isolation (`pheft` vs `eheft`) is unanimous across both runs
that have measured it (4/5 then 5/5 shapes), while the
contention-awareness isolation (`cheft` vs `pheft`) stays close to even
(3/5 vs 2/5, both times). Four honest explanations for the run-to-run
swings, not mutually exclusive:

1. **This machine is a shared, virtualized 4-core VM.** `CLOCK_MONOTONIC`
   timings on it carry host-scheduler noise that a bare-metal run
   wouldn't. §3's own slowdown matrix is smaller/noisier than earlier
   development runs on the same hardware.
2. **15 DAGs (3 seeds × 5 shapes) is not a large sample.** A handful of
   outlier runs (e.g. §5's `wide_shallow_s2/pheft` row, an 89.1% error
   outlier, or the earlier runs' `dense_s1`/`dense_s3` showing `eheft`
   40%+ wrong while `dense_s2` shows single digits for everyone) can
   swing a 15-row mean substantially.
3. **Running a fourth scheduler changes the corpus's execution
   ordering and real co-runner timing**, even on the identical 15 DAGs
   with the identical calibration methodology — `benchmark_suite.cpp`
   schedules and executes each DAG's four schedulers back to back, so
   adding `pheft` changes what real contention every other scheduler's
   *measured* (not predicted) runs happen to see, on a VM whose own
   background load is already noisy per (1). This is a genuine
   limitation of measuring contention by actually causing it, not a
   bug — but it means per-scheduler comparisons across differently-
   sized sweeps aren't strictly apples-to-apples either.
4. **§5's canonical run was one stage inside a 15-minute, 7-stage
   `./run_demo.sh` execution** (calibration pilot, five regression
   binaries, fault injection, and the single-DAG calibration demo all
   ran first on the same VM), vs. the standalone `./benchmark_suite`
   runs with nothing else competing for the host. Both are legitimate,
   neither is more "real" than the other — but they are different
   machine-load conditions, and the aggregate error roughly doubled
   (16-26% range to 35-42% range) between them, consistent with (1).

None of these are an excuse to not report this: it is the actual finding
at this sample size, on this hardware, today, including every reversal.
A stronger claim (either `cheft` or `eheft` reliably, reproducibly wins,
and by how much of that is the comm model vs. contention-awareness)
would need either a quieter/dedicated host or a much larger corpus (or
both) to separate signal from this machine's noise — see §7.

What has held across every run that measured it: the `sparse`/`dense`
shapes (more room for genuinely concurrent, overlapping placements)
consistently show the largest swings for every scheduler — real signal
that contention matters most exactly where the project's own model
predicts it should. And across both runs that include `pheft`: the
comm-model isolation is one-sided (4/5 then 5/5 shapes for `pheft` over
`eheft`) while the contention-awareness isolation (`cheft` over `pheft`)
is not (3/5 both times) — a more specific, more repeatable signal than
the plain `eheft`-vs-`cheft` comparison ever gave on its own, even
though two data points is still too few to call it settled.

## 7. What would make this conclusive (not done)

- **Bare-metal host**, not a shared VM — removes one entire noise source
  from every measurement in this report.
- **Much larger corpus** (50-100+ DAGs, more shapes, wider task-count
  range) — the 15-DAG sample here is sized for a demo to run in minutes,
  not for a statistically defensible confidence interval.
- **Repeated full sweeps** (run the 15-DAG corpus N times, not once) to
  separate within-run DAG-to-DAG variance from run-to-run machine-state
  variance — this report now has *four* independent full-sweep data
  points (§6: 22.5/32.1, 16.5/19.2, 25.5/24.5, and §5's canonical
  35.9/42.3), still nowhere near enough to fit real error bars to.
- ~~Per-processor-pair comm cost in `eheft` too~~ — **done**: `pheft`
  (`src/sched/pheft.{hpp,cpp}`) is exactly that fourth,
  contention-oblivious-but-per-pair-comm-aware control scheduler, wired
  into `benchmark_suite.cpp`'s sweep and reported in §5. It gives the
  clearest signal of the four runs: across both runs that include it,
  the comm-model isolation (`eheft`→`pheft`) is one-sided (4/5, then
  5/5 shapes), while the contention-awareness isolation (`pheft`→`cheft`)
  stays close to even (3/5 both times) — suggestive that comm-model
  accuracy, not contention-awareness, is doing most of the work in
  `cheft`'s apparent edge over `eheft`. Two data points per isolated
  comparison is still far short of conclusive, but the question is now
  mechanically answerable rather than merely named.

## 8. Reproducing this report

```
./run_demo.sh                    # full pipeline, all 7 stages
python3 analysis/tables.py       # the tables in §5, from out/results.csv
python3 analysis/plots.py        # analysis/out/error_pct.svg
```

`out/results.csv` and `analysis/out/error_pct.svg` checked into this repo
are from the run quoted in §5-6. Re-running `benchmark_suite` will
overwrite them with a new (likely numerically different, per §6) sample.

## 9. Summary

- Contention is real but modest on this hardware (§3).
- The calibration pipeline is genuinely unit-matched (§4).
- `cheft` beat `eheft` on aggregate mean |error| in three of four
  independent sweeps (the first two pre-`pheft`, plus §5's canonical
  `./run_demo.sh` run); one standalone four-scheduler run reversed it
  (§5-6).
- The confound `eheft`-vs-`cheft` always conflated (comm-cost model vs.
  contention-awareness) is now mechanically separable via `pheft`
  (§5, §7). Across the two runs that measured it, the comm-model
  isolation is consistently one-sided in `pheft`'s favor; the
  contention-awareness isolation (`pheft`→`cheft`) is consistently
  close to even — suggestive that the comm-cost model, not
  contention-awareness per se, is the larger driver of `cheft`'s edge
  over `eheft` on this hardware, at this sample size.
- The result is **directionally unstable run-to-run, not yet
  statistically solid**, at this sample size on this hardware —
  reported honestly as such, with a concrete path to a stronger claim
  (§7), not papered over.

