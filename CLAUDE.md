# HEFT-Sim

## What this is
Contention-aware DAG scheduler on real threads. Research claim: HEFT's
cost model w(task, processor) ignores co-runner interference; we measure
that error and correct for it.

## Non-negotiable invariants
- All worker threads are pinned to explicit cores (sched_setaffinity).
- Timing uses CLOCK_MONOTONIC. Never wall clock. Never sleep() as work.
- Tasks perform genuine computation. No simulated durations, ever.
- Every scheduler emits a PREDICTED makespan before execution. Predicted
  vs measured is the primary metric.
- The event log is append-only and is the single source of truth for
  all metrics. Metrics are computed offline from the log, never inline.

## Layout
src/dag/        Task, DAG, generators, Topcuoglu reference DAG
src/sched/      Scheduler interface + naive, heft, eheft, cheft
src/exec/       Thread pools, affinity, dispatcher, event log
src/kernels/    Real work: compute-bound and memory-bound tasks
src/ft/         Fault injector, heartbeat monitor, recovery
src/profile/    Pressure generator, probe, sigma/pi extraction
analysis/       Python: plots, tables
tests/

## Conventions
- Schedulers implement one interface; they are interchangeable.
- No scheduler touches threads. No executor makes placement decisions.
- New behaviour needs a test before it's merged.

## Current phase
Phase 6 (src/ft/, kernel chunking, HeartbeatRegistry, fault injection +
recovery): landed as described below; its commit had mistakenly put the
entire src/ tree under out/src/ instead of src/, fixed (git mv, no
source changed).

Phase 7 (src/profile/ sigma/pi): landed. pressure.{hpp,cpp} (reusable
background-load thread, generalized from contention_pilot.cpp's
interferer_fn), probe.{hpp,cpp} (measure_solo/measure_contended,
reporter/interferer methodology), calibration.{hpp,cpp} +
calibrate_sweep.cpp (CalibrationProfile; calibrate() sweeps
sigma(kernel, processor) and pi(reporter, interferer) kernel pairs).

Phase 8 (src/profile/ regression test): tests/fixtures/profile_calibration.cpp,
33 structural checks (positivity/finiteness, cost()/comm_cost() range
rejection, measure_contended/measure_handoff same-core rejection) --
real timings vary by machine, so this checks invariants, not exact
numbers, unlike tests/fixtures/topcuoglu.cpp's paper-exact checks.

Phase 9 (src/profile/ comm calibration): comm_probe.{hpp,cpp} measures
real cross-core handoff latency (producer/consumer ping-pong, busy-wait
synchronized, release/acquire on a sequence number). CalibrationProfile
gained comm[from][to] (full processor-pair matrix, diagnostic) and
mean_comm_cost() (single scalar -- src/sched/heft.cpp's cost model takes
one flat comm_cost per edge, not a pair matrix). Wired into
src/sched/eheft.hpp::calibrate_costs(), replacing the previous hardcoded
0.0 edge cost.

Phase 10 (src/sched/eheft -- empirical HEFT): landed. eheft.{hpp,cpp}:
calibrate_costs() substitutes a topology's Task::cost from a
CalibrationProfile (ignoring the topology's own cost table) and its edge
comm_cost from mean_comm_cost(); eheft_schedule()/EheftScheduler run
heft_schedule() (src/sched/heft.cpp unmodified) on the result. Still
contention-oblivious by design -- it's the fair baseline cheft is meant
to beat. tests/fixtures/eheft.cpp: 25 hand-computed checks (small 3-task
fixture, worked rank_u/EFT/makespan by hand). tests/fixtures/
topcuoglu_calibrated.cpp refactored to use EheftScheduler instead of a
duplicated fixture-specific DAG builder; prints sigma, pi, mean comm
cost, and a genuinely unit-matched predicted-vs-measured error% --
resolves topcuoglu_exec.cpp's calibration caveat (reworded to point
here).

Phase 12 (src/dag/generators): landed. generate_layered_dag() produces
reproducible (seeded) random layered DAGs -- edges only between adjacent
layers (acyclic by construction, no cycle check needed), every
non-layer-0 task guaranteed >=1 predecessor. tests/fixtures/generators.cpp:
745 checks swept across seeds/configs (including edge_probability 0.0
and 1.0 extremes, bad-config rejection).

Phase 6 fault-tolerance details (unchanged): Dispatcher::enable_fault_tolerance()
kills one random task's first attempt and recovers it once via
heartbeat-staleness detection; FAILURE_DETECTED/RECOVERY_COMPLETE are
logged. tests/fixtures/topcuoglu_ft covers the completion-vs-failure
CAS race (dispatcher.hpp hazards #9-#11), stress-tested 45+ runs
across every task id including the entry task, no flakes.

Next: Phase 11 (src/sched/cheft -- contention-aware scheduler consuming
sigma/pi, meant to beat eheft's predicted-vs-measured error on the same
workload) and Phase 13/14 (benchmark harness running naive/heft/eheft/
cheft across many generated DAGs into a machine-readable results log,
plus analysis/ turning that into the paper's plots/tables) -- none of
these exist yet.