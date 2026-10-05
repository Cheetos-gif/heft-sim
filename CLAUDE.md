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
Phase 6: src/ft/ (fault_injector, heartbeat_monitor, recovery) + kernel
chunking (src/kernels heartbeat_chunks) + HeartbeatRegistry
(src/exec/heartbeat.hpp) landed. Dispatcher::enable_fault_tolerance()
kills one random task's first attempt and recovers it once via
heartbeat-staleness detection; FAILURE_DETECTED/RECOVERY_COMPLETE are
logged. tests/fixtures/topcuoglu_ft covers the completion-vs-failure
CAS race (dispatcher.hpp hazards #9-#11), stress-tested 45+ runs
across every task id including the entry task, no flakes. Next:
src/profile (needed before error% in topcuoglu_exec is a real
calibrated number -- see that runner's printed caveat).