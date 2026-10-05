// src/exec/heartbeat.cpp
#include "heartbeat.hpp"

namespace heftsim {

HeartbeatRegistry::HeartbeatRegistry(std::size_t num_tasks) {
    last_beat_ = std::vector<std::atomic<double>>(num_tasks);
    status_ = std::vector<std::atomic<int>>(num_tasks);
    attempt_ = std::vector<std::atomic<int>>(num_tasks);
    for (std::size_t i = 0; i < num_tasks; ++i) {
        last_beat_[i].store(0.0, std::memory_order_relaxed);
        status_[i].store(static_cast<int>(Status::NOT_STARTED), std::memory_order_relaxed);
        attempt_[i].store(0, std::memory_order_relaxed);
    }
}

void HeartbeatRegistry::start(int task_id, int attempt, double now) {
    // Sole writer at this point in time: only the worker about to run
    // this specific (task_id, attempt) calls start() for it, and it does
    // so exactly once per attempt, so a plain store (not a CAS) is
    // correct here -- there is no concurrent writer to race against.
    attempt_[task_id].store(attempt, std::memory_order_relaxed);
    last_beat_[task_id].store(now, std::memory_order_relaxed);
    status_[task_id].store(static_cast<int>(Status::RUNNING), std::memory_order_release);
}

void HeartbeatRegistry::beat(int task_id, double now) {
    last_beat_[task_id].store(now, std::memory_order_relaxed);
}

bool HeartbeatRegistry::try_finish(int task_id) {
    int expected = static_cast<int>(Status::RUNNING);
    return status_[task_id].compare_exchange_strong(expected, static_cast<int>(Status::FINISHED),
                                                      std::memory_order_acq_rel);
}

bool HeartbeatRegistry::try_fail(int task_id) {
    int expected = static_cast<int>(Status::RUNNING);
    return status_[task_id].compare_exchange_strong(expected, static_cast<int>(Status::FAILED),
                                                      std::memory_order_acq_rel);
}

double HeartbeatRegistry::last_beat(int task_id) const {
    return last_beat_[task_id].load(std::memory_order_relaxed);
}

bool HeartbeatRegistry::is_running(int task_id) const {
    return status_[task_id].load(std::memory_order_acquire) == static_cast<int>(Status::RUNNING);
}

int HeartbeatRegistry::current_attempt(int task_id) const {
    return attempt_[task_id].load(std::memory_order_relaxed);
}

}  // namespace heftsim
