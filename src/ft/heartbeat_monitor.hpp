// src/ft/heartbeat_monitor.hpp
//
// Polls src/exec/heartbeat.hpp's HeartbeatRegistry and declares a task
// failed if its heartbeat hasn't advanced for n_intervals * interval_sec.
// This is a generic staleness detector -- it has no idea whether a given
// staleness was caused by FaultInjector's deliberate crash simulation or
// anything else, which is exactly what makes it a faithful detector: real
// crashes and injected ones look identical to it.
//
// ------------------------------------------------------------- the race
//
// A worker can be one heartbeat away from finishing task T at the exact
// moment this monitor's poll sees T's heartbeat as stale. Both threads
// then race to decide T's fate. This is resolved with the same technique
// already used for successor-release races (dispatcher.hpp hazard #1):
// the decision is the *return value* of a single atomic compare-exchange
// off RUNNING (HeartbeatRegistry::try_fail / try_finish), never a
// separate load-then-branch. Exactly one side ever wins for a given task:
//   - Monitor wins (try_fail succeeds): log FAILURE_DETECTED, call
//     on_failed(task_id) exactly once. The worker's own eventual
//     try_finish() will then fail and it silently discards its result.
//   - Worker wins (try_finish succeeds first): this monitor's try_fail()
//     on the same poll simply fails and does nothing -- no false failure
//     is ever declared once a task has genuinely completed.
// The staleness *check* itself (comparing timestamps) can be arbitrarily
// stale or racy with no correctness cost -- it only decides whether to
// *attempt* the CAS; only the CAS result is ever trusted.
//
// A task is only ever watched while it's on attempt 0 (current_attempt()
// == 0): once recovery starts a task's attempt 1, this monitor stops
// polling it, matching src/ft/recovery.hpp's "at most one retry" policy
// and guaranteeing on_failed() is never invoked twice for the same task.

#pragma once

#include "../exec/event_log.hpp"
#include "../exec/heartbeat.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>

namespace heftsim {

class HeartbeatMonitor {
public:
    // registry and log must outlive this monitor. on_failed(task_id) is
    // invoked synchronously, on the monitor's own thread, immediately
    // after this monitor wins the try_fail() race for task_id -- it must
    // be safe to call from a thread other than the caller of start().
    HeartbeatMonitor(HeartbeatRegistry& registry, EventLog& log, std::size_t num_tasks, double t0,
                      double heartbeat_interval_sec, int n_intervals, std::function<void(int)> on_failed);
    ~HeartbeatMonitor();

    HeartbeatMonitor(const HeartbeatMonitor&) = delete;
    HeartbeatMonitor& operator=(const HeartbeatMonitor&) = delete;

    void start();  // spawns the polling thread; call once
    void stop();   // signals shutdown and joins; safe to call once, idempotent otherwise

private:
    void poll_loop();

    HeartbeatRegistry& registry_;
    EventLog& log_;
    std::size_t num_tasks_;
    double t0_;
    double interval_sec_;
    double threshold_sec_;

    std::function<void(int)> on_failed_;

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
};

}  // namespace heftsim
