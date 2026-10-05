// src/ft/heartbeat_monitor.cpp
#include "heartbeat_monitor.hpp"

#include <ctime>

namespace heftsim {

namespace {

double now_sec() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);  // never wall clock (CLAUDE.md)
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

}  // namespace

HeartbeatMonitor::HeartbeatMonitor(HeartbeatRegistry& registry, EventLog& log, std::size_t num_tasks, double t0,
                                    double heartbeat_interval_sec, int n_intervals, std::function<void(int)> on_failed)
    : registry_(registry),
      log_(log),
      num_tasks_(num_tasks),
      t0_(t0),
      interval_sec_(heartbeat_interval_sec),
      threshold_sec_(heartbeat_interval_sec * n_intervals),
      on_failed_(std::move(on_failed)) {}

HeartbeatMonitor::~HeartbeatMonitor() { stop(); }

void HeartbeatMonitor::start() {
    thread_ = std::thread(&HeartbeatMonitor::poll_loop, this);
}

void HeartbeatMonitor::stop() {
    if (stop_.exchange(true, std::memory_order_acq_rel)) return;  // already stopped/stopping
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void HeartbeatMonitor::poll_loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_.load(std::memory_order_acquire)) {
        cv_.wait_for(lock, std::chrono::duration<double>(interval_sec_));
        if (stop_.load(std::memory_order_acquire)) break;

        double now = now_sec() - t0_;
        for (std::size_t t = 0; t < num_tasks_; ++t) {
            int task_id = static_cast<int>(t);
            // Only ever watch a task's *first* attempt -- see recovery.hpp
            // and this header's race-reasoning comment for why watching
            // attempt 1 as well would risk a second on_failed() call.
            if (registry_.current_attempt(task_id) != 0) continue;
            if (!registry_.is_running(task_id)) continue;

            double elapsed = now - registry_.last_beat(task_id);
            if (elapsed <= threshold_sec_) continue;

            // The staleness check above is just a trigger; try_fail() is
            // the one race-free decision (see header comment). Losing it
            // here means the worker finished first -- nothing to do.
            if (registry_.try_fail(task_id)) {
                log_.append(Event{task_id, -1, EventType::FAILURE_DETECTED, now});
                on_failed_(task_id);
            }
        }
    }
}

}  // namespace heftsim
