// tests/fixtures/topcuoglu_ft.cpp
//
// Regression test + demo for src/ft/: run the Topcuoglu fixture DAG under
// HEFT with fault tolerance enabled. FaultInjector kills exactly one
// randomly (seeded, for reproducibility) chosen task's first attempt;
// HeartbeatMonitor must notice via heartbeat staleness, log
// FAILURE_DETECTED, and trigger exactly one recovery attempt that
// completes normally and logs RECOVERY_COMPLETE. See
// src/exec/dispatcher.hpp hazards #9-#11 for the concurrency reasoning
// this exercises (completion-vs-failure race, cross-attempt Workspace
// isolation, at-most-one-retry).
//
// Kernel sizes here are deliberately light and finely chunked
// (heartbeat_chunks) relative to topcuoglu_exec.cpp's: fault tolerance
// needs many short heartbeats per task, not one long uninterruptible
// kernel call.
//
// Build:  g++ -std=c++17 -O2 -pthread -Wall -Wextra -Isrc -o
//         tests/fixtures/topcuoglu_ft tests/fixtures/topcuoglu_ft.cpp
//         src/dag/dag.cpp src/sched/heft.cpp src/kernels/kernel.cpp
//         src/exec/affinity.cpp src/exec/event_log.cpp
//         src/exec/heartbeat.cpp src/exec/dispatcher.cpp
//         src/ft/fault_injector.cpp src/ft/heartbeat_monitor.cpp
// Run:    ./tests/fixtures/topcuoglu_ft

#include "../../src/exec/dispatcher.hpp"
#include "../../src/sched/heft.hpp"
#include "topcuoglu_dag.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace heftsim;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

const char* event_name(EventType t) {
    switch (t) {
        case EventType::READY: return "READY";
        case EventType::START: return "START";
        case EventType::FINISH: return "FINISH";
        case EventType::FAILURE_DETECTED: return "FAILURE_DETECTED";
        case EventType::RECOVERY_COMPLETE: return "RECOVERY_COMPLETE";
    }
    return "?";
}

}  // namespace

int main() {
    Dag dag = fixtures::build_topcuoglu_dag();
    HeftResult prediction = heft_schedule(dag);
    Schedule schedule{prediction.schedule, prediction.makespan};

    KernelConfig cfg;
    cfg.mem_buf_bytes = 2ull << 20;  // 2 MB
    cfg.mem_steps = 200'000;
    cfg.comp_doubles = 256;
    cfg.comp_iters = 6'000;
    cfg.heartbeat_chunks = 40;  // many short slices so a crash is detectable quickly

    Dispatcher dispatcher(dag, schedule, cfg);
    dispatcher.enable_fault_tolerance(FaultToleranceConfig{/*heartbeat_interval_sec=*/0.004, /*n_intervals=*/5},
                                       /*seed=*/42);

    int victim = dispatcher.fault_victim();
    check(victim >= 0 && victim < static_cast<int>(dag.num_tasks()), "fault_victim() is a valid task id");
    std::printf("fault injector will kill task %d on its first attempt\n", victim);

    ExecutionResult result = dispatcher.run();

    std::printf("\n%zu tasks executed, %zu events logged\n", result.tasks.size(), result.log.size());
    for (const auto& ev : result.log) {
        if (ev.type == EventType::FAILURE_DETECTED || ev.type == EventType::RECOVERY_COMPLETE) {
            std::printf("  t=%8.5f  task=%d  %s\n", ev.timestamp, ev.task, event_name(ev.type));
        }
    }

    // Every task genuinely finished, including the victim (via recovery).
    check(result.tasks.size() == dag.num_tasks(), "all tasks have a recorded completion");

    // Precedence still holds end to end, same invariant topcuoglu_exec.cpp
    // checks -- fault tolerance must never let a successor start before
    // its predecessor's *official* (winning) completion.
    bool precedence_ok = true;
    for (const auto& e : result.tasks) {
        for (int pred : dag.task(e.task).preds) {
            if (e.start + 1e-9 < result.tasks[pred].finish) {
                std::fprintf(stderr, "PRECEDENCE VIOLATION: task %d started before predecessor %d finished\n", e.task,
                             pred);
                precedence_ok = false;
            }
        }
    }
    check(precedence_ok, "precedence respected despite the injected fault");

    // Exactly one failure, exactly one recovery, both for the victim task,
    // and exactly one FINISH per task (the killed attempt never logs one).
    int failure_count = 0, recovery_count = 0;
    std::vector<int> finish_count(dag.num_tasks(), 0);
    for (const auto& ev : result.log) {
        if (ev.type == EventType::FAILURE_DETECTED) {
            ++failure_count;
            check(ev.task == victim, "FAILURE_DETECTED names the injected victim");
        } else if (ev.type == EventType::RECOVERY_COMPLETE) {
            ++recovery_count;
            check(ev.task == victim, "RECOVERY_COMPLETE names the injected victim");
        } else if (ev.type == EventType::FINISH) {
            ++finish_count[ev.task];
        }
    }
    check(failure_count == 1, "exactly one FAILURE_DETECTED event");
    check(recovery_count == 1, "exactly one RECOVERY_COMPLETE event");
    for (int t = 0; t < static_cast<int>(dag.num_tasks()); ++t) {
        check(finish_count[t] == 1, "task " + std::to_string(t) + " has exactly one FINISH event");
    }

    // FAILURE_DETECTED must precede RECOVERY_COMPLETE in time (recovery is
    // a reaction to detection, never the reverse).
    double t_failure = -1, t_recovery = -1;
    for (const auto& ev : result.log) {
        if (ev.type == EventType::FAILURE_DETECTED) t_failure = ev.timestamp;
        if (ev.type == EventType::RECOVERY_COMPLETE) t_recovery = ev.timestamp;
    }
    check(t_failure >= 0 && t_recovery >= 0 && t_failure < t_recovery,
          "FAILURE_DETECTED happens before RECOVERY_COMPLETE");

    std::printf("\nmeasured makespan: %.4f s\n", result.measured_makespan);
    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
