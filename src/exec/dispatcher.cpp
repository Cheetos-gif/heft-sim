// src/exec/dispatcher.cpp
#include "dispatcher.hpp"

#include "../ft/heartbeat_monitor.hpp"
#include "../ft/recovery.hpp"
#include "affinity.hpp"

#include <algorithm>
#include <ctime>
#include <thread>

namespace heftsim {

namespace {

double now_sec() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);  // never wall clock (CLAUDE.md)
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

}  // namespace

Dispatcher::Dispatcher(const Dag& dag, const Schedule& schedule, KernelConfig cfg)
    : dag_(dag), schedule_(schedule), cfg_(cfg), heartbeat_(dag.num_tasks()) {
    const std::size_t n = dag_.num_tasks();
    const std::size_t p = dag_.num_processors();

    workspaces_[0] = std::vector<Workspace>(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Task& t = dag_.task(static_cast<int>(i));
        init_workspace(t.kernel, workspaces_[0][i], /*seed=*/static_cast<unsigned>(i) + 1, cfg_);
    }
    // workspaces_[1] (the recovery-attempt slot) is left empty until/unless
    // enable_fault_tolerance() allocates it -- see hazard #10.

    remaining_preds_ = std::vector<std::atomic<int>>(n);
    for (std::size_t i = 0; i < n; ++i) {
        remaining_preds_[i].store(static_cast<int>(dag_.task(static_cast<int>(i)).preds.size()),
                                   std::memory_order_relaxed);
    }

    queue_mutex_ = std::vector<std::mutex>(p);
    queue_cv_ = std::vector<std::condition_variable>(p);
    ready_queue_.resize(p);
    results_.resize(n);

    retried_ = std::vector<std::atomic<bool>>(n);
    for (std::size_t i = 0; i < n; ++i) retried_[i].store(false, std::memory_order_relaxed);
}

void Dispatcher::enable_fault_tolerance(FaultToleranceConfig ft_config, unsigned seed) {
    ft_cfg_ = ft_config;
    const std::size_t n = dag_.num_tasks();
    fault_injector_ = std::make_unique<FaultInjector>(n, seed);

    // Allocated for every task, not only the victim -- see hazard #10:
    // HeartbeatMonitor's staleness check is generic and could in
    // principle fire on any task, so any task must have a safe attempt-1
    // Workspace to retry into.
    workspaces_[1] = std::vector<Workspace>(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Task& t = dag_.task(static_cast<int>(i));
        init_workspace(t.kernel, workspaces_[1][i], /*seed=*/static_cast<unsigned>(i) + 1001, cfg_);
    }
}

int Dispatcher::fault_victim() const { return fault_injector_ ? fault_injector_->victim() : -1; }

void Dispatcher::enqueue(int task_id, int attempt) {
    int proc = schedule_.schedule[task_id].processor;
    log_.append(Event{task_id, proc, EventType::READY, now_sec() - t0_});
    {
        std::lock_guard<std::mutex> lock(queue_mutex_[proc]);
        ready_queue_[proc].push_back(QueuedTask{task_id, attempt});
    }
    queue_cv_[proc].notify_one();
}

void Dispatcher::retry(int task_id) {
    // Idempotency guard independent of the monitor's own "only watch
    // attempt 0" filter -- defense in depth against ever double-enqueuing
    // the same task (hazard #11).
    if (retried_[task_id].exchange(true, std::memory_order_acq_rel)) return;
    enqueue(task_id, kMaxRecoveryAttempts);
}

void Dispatcher::execute(int task_id, int attempt, int processor) {
    double t_start = now_sec() - t0_;
    log_.append(Event{task_id, processor, EventType::START, t_start});
    heartbeat_.start(task_id, attempt, t_start);

    Workspace& ws = workspaces_[attempt][task_id];
    KernelProgress progress;
    bool done = false;
    while (!done) {
        done = run_kernel_chunk(dag_.task(task_id).kernel, ws, cfg_, progress);
        double t_beat = now_sec() - t0_;
        heartbeat_.beat(task_id, t_beat);
        if (fault_injector_ && fault_injector_->should_kill(task_id, attempt)) {
            // Simulated crash: stop making progress and abandon this
            // attempt. No FINISH is ever logged for it; the heartbeat
            // monitor's staleness timeout is what notices and recovers it
            // (hazard #9/#10).
            return;
        }
    }

    if (!heartbeat_.try_finish(task_id)) {
        // Lost the completion-vs-failure race: the monitor already
        // declared this task FAILED and a recovery attempt is queued or
        // running on its own Workspace slot. Discard this result -- only
        // the CAS winner ever publishes (hazard #9).
        return;
    }

    double t_finish = now_sec() - t0_;
    log_.append(Event{task_id, processor, EventType::FINISH, t_finish});
    if (attempt > 0) {
        log_.append(Event{task_id, processor, EventType::RECOVERY_COMPLETE, t_finish});
    }
    results_[task_id] = ExecutedTask{task_id, processor, t_start, t_finish};

    // Release successors. Exactly one thread ever observes each
    // remaining_preds_[s] transition to zero (see dispatcher.hpp hazard
    // #1), so exactly one enqueue happens per successor.
    for (int succ : dag_.successors(task_id)) {
        if (remaining_preds_[succ].fetch_sub(1, std::memory_order_acq_rel) == 1) {
            enqueue(succ, 0);
        }
    }

    // Shutdown: the thread that finishes the last task anywhere wakes
    // every worker so idle processors don't block forever (hazard #3).
    // Only a winning try_finish() reaches here, so remaining_tasks_ can
    // never reach zero while a task is mid-recovery (hazard #11).
    if (remaining_tasks_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        stopped_.store(true, std::memory_order_release);
        for (auto& cv : queue_cv_) cv.notify_all();
    }
}

void Dispatcher::worker_loop(int processor, int core) {
    if (!pin_current_thread(core)) {
        all_pinned_.store(false, std::memory_order_relaxed);
    }

    for (;;) {
        QueuedTask qt;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_[processor]);
            queue_cv_[processor].wait(lock, [&] {
                return !ready_queue_[processor].empty() || stopped_.load(std::memory_order_acquire);
            });
            if (ready_queue_[processor].empty()) {
                // stopped_ is set and nothing is left for this processor.
                break;
            }
            qt = ready_queue_[processor].front();
            ready_queue_[processor].pop_front();
        }
        execute(qt.task, qt.attempt, processor);
    }
}

ExecutionResult Dispatcher::run() {
    const std::size_t n = dag_.num_tasks();
    const std::size_t p = dag_.num_processors();

    remaining_tasks_.store(static_cast<int>(n), std::memory_order_relaxed);
    t0_ = now_sec();

    std::unique_ptr<HeartbeatMonitor> monitor;
    if (fault_injector_) {
        monitor = std::make_unique<HeartbeatMonitor>(heartbeat_, log_, n, t0_, ft_cfg_.heartbeat_interval_sec,
                                                       ft_cfg_.n_intervals, [this](int task_id) { retry(task_id); });
        monitor->start();
    }

    // Seed entry tasks directly: their remaining_preds_ is already 0 and
    // was never reached via the fetch_sub path, so they must be queued
    // explicitly, once, before any worker starts.
    for (int t : dag_.entry_tasks()) enqueue(t, 0);

    std::vector<std::thread> threads;
    threads.reserve(p);
    for (std::size_t proc = 0; proc < p; ++proc) {
        threads.emplace_back(&Dispatcher::worker_loop, this, static_cast<int>(proc), core_for_processor(static_cast<int>(proc)));
    }
    // Unconditional join, including on the exception path out of this
    // scope: no worker lambda may outlive this stack frame's members
    // (hazard #7).
    for (auto& th : threads) {
        if (th.joinable()) th.join();
    }
    // Same unconditional-teardown discipline for the monitor thread: it
    // captures `this` too, and must not outlive this stack frame.
    if (monitor) monitor->stop();

    ExecutionResult result;
    result.tasks = results_;
    result.log = log_.snapshot();
    result.all_pinned = all_pinned_.load(std::memory_order_relaxed);
    double makespan = 0.0;
    for (const auto& r : results_) makespan = std::max(makespan, r.finish);
    result.measured_makespan = makespan;
    return result;
}

}  // namespace heftsim
