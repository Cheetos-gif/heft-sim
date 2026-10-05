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
Every component in this file's Layout section exists and is tested:
src/dag (Dag + generate_layered_dag), src/sched (naive/heft/eheft/cheft),
src/exec (Dispatcher, EventLog, ResultsLog), src/kernels (COMPUTE_BOUND/
MEMORY_BOUND), src/ft (fault injection + heartbeat recovery), src/profile
(sigma/pi/pi3/comm calibration), analysis (tables.py/plots.py).
924 regression checks pass across tests/fixtures/{topcuoglu 57, eheft
25, cheft 33, profile_calibration 46, generators 745, topcuoglu_ft 18};
full ./run_demo.sh (7 stages, build through a 15-DAG benchmark sweep +
analysis) verified end-to-end on WSL.

Findings, methodology, and -- critically -- the honest limitations of
the current result (run-to-run variance on a 15-DAG sample, this
machine's own noise floor) are written up in REPORT.md, not here. Read
that before touching src/sched/cheft.hpp or benchmark_suite.cpp.

Key design decisions worth knowing before changing scheduling code:
- eheft and cheft both rank tasks (rank_u) using calibrated-but-
  contention-oblivious costs; contention correction happens only at EFT
  insertion in cheft, never at ranking (no principled pre-placement
  contention estimate exists -- see src/sched/cheft.hpp).
- eheft is forced through heft.cpp's one-scalar-per-edge Dag
  representation, so it uses mean_comm_cost(); cheft has its own
  insertion loop and uses per-processor-pair comm_cost() directly.
- cheft's contention multiplier uses a genuine 3-way measurement (pi3)
  when exactly 2 processors have overlapping tasks, pairwise pi for
  exactly 1, and max-pairwise only as an unvalidated fallback for >=3
  (unreachable below num_processors==5).
- heft.cpp itself is never modified by eheft or cheft -- both duplicate
  its small find_earliest_slot() rather than touch the reference
  contention-oblivious baseline.

Not started (see REPORT.md §7 for why each matters): bare-metal
(non-virtualized) measurement; a corpus larger than 15 DAGs; repeated
full sweeps to separate DAG-to-DAG variance from run-to-run machine
noise; per-processor-pair comm cost in eheft too (an isolating control
for how much of cheft's edge is comm-model vs contention-awareness).