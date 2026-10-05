// src/exec/dispatcher.hpp
//
// Executes a HEFT schedule on real pinned threads. One worker thread per
// processor; a task is released to its assigned processor's ready queue
// the instant its last predecessor finishes -- never on a wall-clock
// schedule. Every state transition is timestamped with CLOCK_MONOTONIC
// and written to one append-only EventLog (src/exec/event_log.hpp).
//
// This intentionally does NOT replay HEFT's predicted insertion order: a
// processor's worker simply drains whatever becomes ready for it, in
// whatever order that happens in real time. If measured kernel costs
// diverge from the predicted cost table enough to reorder readiness, the
// realized schedule legitimately diverges from the prediction -- that
// divergence is the thing this project exists to measure (CLAUDE.md:
// "Predicted vs measured is the primary metric").
//
// ---------------------------------------------------------------- design
//
// Threads: dag.num_processors() worker threads, each pinned to a distinct
// core via pin_current_thread() (src/exec/affinity.hpp). The calling
// thread only builds state, spawns workers, and joins; it never touches a
// ready queue itself except to seed entry tasks before any worker starts.
//
// Readiness: remaining_preds_[t] is an atomic<int> initialized to
// dag.task(t).preds.size(). A worker that finishes task t calls
// fetch_sub(1) on remaining_preds_[s] for every successor s; the thread
// that observes the pre-decrement value == 1 made s ready, and only that
// thread enqueues s.
//
// ------------------------------------------------------ race/lifetime hazards
//
// 1. Double-enqueue / double-execution of a successor task.
//    Hazard: two predecessors of the same task finish concurrently and
//    both conclude "I was last, I'll enqueue it."
//    Handled: readiness is decided purely by the *return value* of one
//    atomic fetch_sub on remaining_preds_[s] -- never by a separate
//    load-then-compare-then-decrement. A single atomic RMW has exactly
//    one caller observe the transition to zero, no matter how many
//    predecessors finish in the same instant, so exactly one enqueue (and
//    thus exactly one execution) happens per task, always.
//
// 2. Lost wakeup on a processor's ready queue.
//    Hazard: a producer pushes a task and notifies in the window between
//    the consumer's "queue empty?" check and its condition_variable::wait
//    call, so the notify is missed and the worker blocks forever.
//    Handled: the push and the predicate check both happen under the same
//    per-processor mutex; consumers use the predicate-checking overload
//    `cv.wait(lock, pred)`, which re-checks pred atomically with respect
//    to the lock before sleeping. A notify that lands after the last
//    check but before the thread is actually asleep can never be missed,
//    because the predicate is re-evaluated under the same lock the
//    producer held while mutating the queue.
//
// 3. Worker blocked forever once there is no more work coming (shutdown).
//    Hazard: after the last task anywhere finishes, idle workers on other
//    processors are parked in cv.wait with nothing left to ever push to
//    them.
//    Handled: a global atomic<int> remaining_tasks_ starts at
//    dag.num_tasks() and is fetch_sub'd by whichever thread finishes each
//    task. The thread that drives it to zero does an atomic release store
//    to stopped_ and notify_all()s every processor's condition variable.
//    Each worker's wait predicate is `!queue.empty() || stopped_`, and it
//    only exits its loop when the queue is *also* empty, so a worker can
//    never abandon a queued task even if it observes stopped_ mid-drain.
//    remaining_tasks_ only reaches zero after every task has already been
//    dequeued and executed, so no queue can still hold work at that point
//    -- shutdown cannot race a legitimate enqueue.
//
// 4. Concurrent writers corrupting the shared EventLog.
//    Hazard: every worker calls log_.append() on READY/START/FINISH
//    transitions from different threads; std::vector::push_back is not
//    thread-safe and can reallocate mid-write.
//    Handled: EventLog serializes every append behind one std::mutex,
//    with the whole push_back inside the critical section (see
//    event_log.cpp). No reader touches the vector while workers are
//    still running -- run() calls log_.snapshot() only after every worker
//    thread has been joined -- so there is no reader/writer overlap
//    either.
//
// 5. Timestamp order vs. log order under contention.
//    Hazard: assuming event N was recorded before event N+1 because it
//    appears earlier in the log.
//    Handled: this does not hold, by construction -- each thread reads
//    CLOCK_MONOTONIC *before* acquiring the log mutex (so lock-queueing
//    time is never charged to a task's own measured cost), so two threads
//    can race for the lock in the opposite order from the order their
//    timestamps would sort into. Documented, not "fixed": every consumer
//    of the log must sort by the `timestamp` field.
//
// 6. Workspace data races / accidental contention experiments.
//    Hazard: reusing one Workspace across tasks or threads would race on
//    its buffers, and would turn a memory-bound task's private
//    pointer-chase into an inadvertent measurement of contention instead
//    of the task's own cost (see contention_pilot.cpp's identical
//    warning about sharing MEMORY workspaces).
//    Handled: Dispatcher owns exactly one Workspace per task id,
//    allocated and touched (init_workspace) once during setup, before
//    t0_ is captured -- so first-fault cost never lands inside a timed
//    window. Each Workspace is subsequently touched by exactly one
//    thread: the worker executing that specific task.
//
// 7. Thread/data lifetime across run().
//    Hazard: worker lambdas capture `this` and references to the Dag and
//    HeftResult passed to the constructor; if those were destroyed (or
//    run() returned) while a thread was still running, that's a
//    use-after-free.
//    Handled: run() is synchronous. It spawns exactly N threads and joins
//    every one of them, in the same stack frame, before constructing or
//    returning ExecutionResult -- including on the exception path (the
//    join loop is unconditional, not inside a try only reached on
//    success). Dag/HeftResult are stored as const references and the
//    constructor's contract is that the caller keeps them alive for the
//    Dispatcher's entire lifetime, exactly like every other component in
//    this codebase that takes `const Dag&`.
//
// 8. Pinning failure silently producing misleading timings.
//    Hazard: pin_current_thread() can fail (fewer physical cores than
//    processors, a sandboxed environment); an unpinned task then floats
//    across cores and its measured cost is contaminated by migration and
//    cold caches, with nothing to say so.
//    Handled: each worker records whether its own pin call succeeded;
//    ExecutionResult::all_pinned is the AND of all of them, so a caller
//    can print a warning instead of silently trusting the numbers -- the
//    same tradeoff contention_pilot.cpp makes (warn, don't abort).
//
// -------------------------------------------------- fault tolerance (src/ft/)
//
// enable_fault_tolerance() opts a run into: FaultInjector kills exactly
// one randomly chosen task's first attempt (stops it from publishing
// heartbeats and abandons it, simulating a crash); HeartbeatMonitor
// (src/ft/heartbeat_monitor.hpp) polls src/exec/heartbeat.hpp's
// HeartbeatRegistry and declares a task failed once its heartbeat has
// been stale for n_intervals * heartbeat_interval_sec; Dispatcher::retry()
// re-enqueues the failed task for exactly one recovery attempt
// (src/ft/recovery.hpp).
//
// 9. Monitor marks a task FAILED while it is concurrently completing.
//    Hazard: a worker can be one heartbeat away from finishing task T at
//    the exact instant the monitor's poll sees T's heartbeat as stale.
//    Both sides must never "win": that would mean either a task that
//    genuinely finished gets wastefully/incorrectly recovered, or (far
//    worse) both the original attempt's completion bookkeeping AND a
//    recovery attempt's completion bookkeeping run for the same task --
//    double successor release, double remaining_tasks_ decrement.
//    Handled: the same technique as hazard #1, reused deliberately --
//    the decision is the *return value* of a single atomic
//    compare-exchange off RUNNING (HeartbeatRegistry::try_finish for the
//    worker, try_fail for the monitor), never a separate
//    load-then-branch. Exactly one of them can ever win for a given
//    task, because once either side moves status off RUNNING, the
//    other's compare-exchange necessarily fails against its own
//    `expected == RUNNING`. The loser does nothing further: a losing
//    worker discards its (possibly genuinely-correct) result without
//    logging FINISH or touching remaining_preds_/remaining_tasks_; a
//    losing monitor poll simply declares no failure. See
//    src/ft/heartbeat_monitor.hpp for the full walkthrough.
//
// 10. Two attempts of the same task touching one Workspace concurrently.
//    Hazard: hazard #9's losing worker can still be mid-kernel (still
//    touching its Workspace) at the exact moment recovery starts a
//    second attempt of the same task. If both attempts shared one
//    Workspace, that's a genuine data race on ws.buf/ws.chase --
//    undefined behavior, not just wasted work.
//    Handled: workspaces_ holds two independent Workspace instances per
//    task, indexed by attempt (workspaces_[0][t], workspaces_[1][t]).
//    Attempt 1 is allocated for *every* task the moment
//    enable_fault_tolerance() is called, not only the designated victim
//    -- the monitor's staleness check is generic and could in principle
//    (under sufficiently adversarial scheduling) fire on any task, not
//    only the one FaultInjector targets. With disjoint memory per
//    attempt, a losing attempt's stray trailing writes are simply
//    harmless: nobody will ever read that Workspace's contents again.
//    This was chosen over a cross-thread wait/notify handshake (blocking
//    the retry until the loser provably stops touching its Workspace)
//    because it is both simpler and impossible to get subtly wrong.
//
// 11. Recovery running twice for the same task / shutdown hanging on a
//     permanently-failed task.
//    Hazard: if try_fail() could somehow succeed twice for one task (a
//    bug in the "only watch attempt 0" filter, or adversarial timing),
//    retry() could enqueue two attempt-1 executions of the same task,
//    recreating hazard #10 on the *second* Workspace slot with no third
//    slot to fall back on. Separately: a task that is failed and never
//    successfully retried would never decrement remaining_tasks_, and
//    run()'s shutdown (gated on remaining_tasks_ reaching zero) would
//    hang forever.
//    Handled (double-retry): retry() itself is guarded by its own
//    one-shot atomic exchange (retried_[task_id]) independent of the
//    monitor's attempt==0 pre-filter -- defense in depth, so even a
//    hypothetical second try_fail() success cannot produce a second
//    enqueue.
//    Handled (no deadlock): remaining_tasks_ is only ever decremented by
//    a *winning* try_finish() -- i.e. only on genuine completion, by
//    whichever attempt succeeds. A task pending recovery has, by
//    definition, not decremented it yet, so remaining_tasks_ cannot
//    reach zero while any task's fate is undecided -- shutdown
//    structurally cannot race a still-pending retry. This falls out of
//    reusing the counter that already existed for hazard #3; no new
//    synchronization was needed to get it.

#include "../dag/dag.hpp"
#include "../ft/fault_injector.hpp"
#include "../kernels/kernel.hpp"
#include "../sched/scheduler.hpp"
#include "event_log.hpp"
#include "heartbeat.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace heftsim {

struct ExecutedTask {
    int task = -1;
    int processor = -1;
    double start = 0.0;   // seconds since run() began, CLOCK_MONOTONIC-based
    double finish = 0.0;
};

struct ExecutionResult {
    std::vector<ExecutedTask> tasks;  // indexed by task id
    std::vector<Event> log;           // final EventLog snapshot, unsorted (see hazard #5)
    double measured_makespan = 0.0;
    bool all_pinned = true;
};

// Tuning for enable_fault_tolerance(); see the "fault tolerance" section
// above. Defaults are sized for this project's demo-scale KernelConfig
// (short kernel chunks): generous enough that a genuinely-progressing
// task's chunk time is nowhere near the staleness threshold.
struct FaultToleranceConfig {
    double heartbeat_interval_sec = 0.004;
    int n_intervals = 5;  // declared failed after this many missed intervals
};

class Dispatcher {
public:
    // dag and schedule must outlive this Dispatcher and every call to
    // run(). schedule must have been computed from dag (schedule.schedule
    // must have one entry per dag task, giving its processor assignment).
    // Any Scheduler implementation works here (src/sched/scheduler.hpp) --
    // Dispatcher never touches threads or makes placement decisions, so
    // it has no reason to know which scheduler produced its input.
    Dispatcher(const Dag& dag, const Schedule& schedule, KernelConfig cfg = KernelConfig{});

    // Opts this run into fault injection + heartbeat-based recovery (see
    // the "fault tolerance" section above). Must be called before run().
    // Picks the victim task immediately -- queryable via fault_victim()
    // right away -- but the monitor thread itself only runs during run().
    void enable_fault_tolerance(FaultToleranceConfig ft_config = FaultToleranceConfig{},
                                 unsigned seed = std::random_device{}());

    // -1 if enable_fault_tolerance() was never called; otherwise the task
    // id FaultInjector will kill on its first attempt.
    int fault_victim() const;

    // Runs the schedule to completion on real pinned threads. Synchronous:
    // blocks until every task has executed. Not reentrant; construct a
    // fresh Dispatcher per run.
    ExecutionResult run();

    // Re-enqueues task_id for exactly one recovery attempt (attempt 1).
    // Called by the heartbeat monitor after it wins the try_fail() race
    // for task_id (hazard #9); idempotent per task (hazard #11) -- safe
    // to call from any thread.
    void retry(int task_id);

private:
    struct QueuedTask {
        int task;
        int attempt;  // 0 = original, 1 = the one recovery retry
    };

    void worker_loop(int processor, int core);
    void execute(int task_id, int attempt, int processor);
    void enqueue(int task_id, int attempt);  // requires remaining_preds_[task_id] == 0

    const Dag& dag_;
    const Schedule& schedule_;
    KernelConfig cfg_;

    std::array<std::vector<Workspace>, 2> workspaces_;  // [attempt][task_id]; attempt 1 empty unless FT enabled
    std::vector<std::atomic<int>> remaining_preds_;
    std::atomic<int> remaining_tasks_{0};
    std::atomic<bool> stopped_{false};
    std::atomic<bool> all_pinned_{true};

    std::vector<std::mutex> queue_mutex_;             // one per processor
    std::vector<std::condition_variable> queue_cv_;   // one per processor
    std::vector<std::deque<QueuedTask>> ready_queue_;  // one per processor

    std::vector<ExecutedTask> results_;  // indexed by task id; each written exactly once
    EventLog log_;
    double t0_ = 0.0;

    // Fault tolerance (opt-in; disabled unless enable_fault_tolerance()
    // is called). heartbeat_ and retried_ always exist (cheap, a handful
    // of atomics) so execute()/retry() have one code path regardless of
    // whether FT is active -- see dispatcher.cpp.
    HeartbeatRegistry heartbeat_;
    std::vector<std::atomic<bool>> retried_;
    std::unique_ptr<FaultInjector> fault_injector_;
    FaultToleranceConfig ft_cfg_;
};

}  // namespace heftsim
