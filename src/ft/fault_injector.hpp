// src/ft/fault_injector.hpp
//
// Picks exactly one task, uniformly at random, to fail on its first
// (attempt 0) execution. "Failing" here means: stop publishing
// heartbeats and stop doing further work for that attempt -- a clean
// simulation of a crash. The fault injector never touches thread state,
// the event log, or the ready queue directly; it only answers
// should_kill(), which src/exec/dispatcher.cpp's execute() polls between
// kernel chunks (see src/exec/heartbeat.hpp for how a worker publishes
// progress, and src/ft/heartbeat_monitor.hpp for who actually detects and
// recovers the resulting staleness).

#pragma once

#include <atomic>
#include <cstddef>
#include <random>

namespace heftsim {

class FaultInjector {
public:
    // Picks a victim task id uniformly from [0, num_tasks). `seed`
    // defaults to a nondeterministic source (a fresh victim each run);
    // tests pass an explicit seed for reproducibility.
    explicit FaultInjector(std::size_t num_tasks, unsigned seed = std::random_device{}());

    int victim() const noexcept { return victim_; }

    // True at most once, ever, across the whole process -- only for the
    // single (victim(), attempt 0) pair, guarded by an atomic exchange so
    // concurrent callers can never both see true. Every other
    // (task_id, attempt) pair -- including the victim's own recovery
    // retry at attempt 1 -- always returns false: exactly one task is
    // killed per run, exactly once.
    bool should_kill(int task_id, int attempt);

private:
    int victim_;
    std::atomic<bool> fired_{false};
};

}  // namespace heftsim
