// src/exec/heartbeat.hpp
//
// The "publish" side of heartbeat-based failure detection: worker threads
// call start()/beat() as they run a task's kernel. src/ft/heartbeat_monitor.*
// is the sole *consumer* -- it decides what staleness means and when to
// act. This class only stores raw state; it has no timeout policy, in
// keeping with CLAUDE.md's event-log philosophy of recording facts, not
// computing metrics inline.
//
// try_finish()/try_fail() are the single race-free decision point for the
// "monitor marks FAILED while the task is concurrently completing" race
// (see src/exec/dispatcher.hpp hazard #9 and src/ft/heartbeat_monitor.hpp):
// both are atomic compare-exchanges off RUNNING, so at most one of them
// ever succeeds for a given task, no matter how they interleave in time.

#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace heftsim {

class HeartbeatRegistry {
public:
    explicit HeartbeatRegistry(std::size_t num_tasks);

    // Called by the worker thread that is about to run task_id's kernel,
    // right before the first chunk -- never at enqueue time, so a task
    // sitting in a ready queue waiting for a free processor is never
    // mistaken for a stalled one. Resets the heartbeat clock, records
    // which attempt this is (0 = original, 1 = the one recovery retry),
    // and marks the task RUNNING.
    void start(int task_id, int attempt, double now);

    // Called by the worker thread after each kernel chunk. Refreshes the
    // heartbeat clock. Harmless no-op if the task is no longer RUNNING.
    void beat(int task_id, double now);

    // Worker's side of the race: atomic RUNNING -> FINISHED. True means
    // this caller is the task's one official completion; false means the
    // monitor already won via try_fail() and this attempt's result must
    // be discarded.
    bool try_finish(int task_id);

    // Monitor's side of the race: atomic RUNNING -> FAILED. True means
    // this caller is the task's one official failure, and it alone should
    // log FAILURE_DETECTED and trigger recovery; false means the worker
    // already finished first.
    bool try_fail(int task_id);

    double last_beat(int task_id) const;
    bool is_running(int task_id) const;
    int current_attempt(int task_id) const;

private:
    enum class Status : int { NOT_STARTED = 0, RUNNING = 1, FINISHED = 2, FAILED = 3 };

    std::vector<std::atomic<double>> last_beat_;
    std::vector<std::atomic<int>> status_;    // Status, stored as int for atomic<int>
    std::vector<std::atomic<int>> attempt_;   // 0 or 1
};

}  // namespace heftsim
