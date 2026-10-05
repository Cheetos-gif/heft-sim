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
recovery) landed as described below, but its commit mistakenly put the
entire src/ tree under out/src/ instead of src/ -- fixed (git mv); no
source changed, only location.

Phase 7: src/profile/ (pressure.{hpp,cpp}, probe.{hpp,cpp},
calibration.{hpp,cpp}) landed -- reporter/interferer methodology from
contention_pilot.cpp lifted into a reusable library. calibrate()
measures sigma(kernel, processor) (median solo seconds, src/profile/probe)
on every processor src/exec/affinity.hpp's core_for_processor() would
actually use, plus pi(reporter, interferer) (contended/solo ratio) on one
representative core pair. New fixture
tests/fixtures/topcuoglu_calibrated.cpp builds the paper DAG's topology
with Task::cost set from sigma instead of the paper's arbitrary units,
runs it through HEFT/naive, executes for real, and prints a genuinely
unit-matched predicted-vs-measured error% -- resolves topcuoglu_exec.cpp's
printed calibration caveat. pi is extracted but not yet consumed by any
scheduler (naive/heft remain intentionally contention-oblivious; that's
what's being measured).

Phase 6 fault-tolerance details (unchanged): Dispatcher::enable_fault_tolerance()
kills one random task's first attempt and recovers it once via
heartbeat-staleness detection; FAILURE_DETECTED/RECOVERY_COMPLETE are
logged. tests/fixtures/topcuoglu_ft covers the completion-vs-failure
CAS race (dispatcher.hpp hazards #9-#11), stress-tested 45+ runs
across every task id including the entry task, no flakes.

Next: cheft (contention-aware scheduler consuming sigma/pi) and/or
analysis/ (Python plots/tables turning out/*.csv into the paper's
predicted-vs-measured error figures) -- neither exists yet.