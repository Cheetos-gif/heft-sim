// tests/fixtures/pheft.cpp
//
// Hand-verified regression test for src/sched/pheft.{hpp,cpp}, the
// confound-isolating control scheduler (REPORT.md §7 / pheft.hpp's
// header comment). pheft = cheft's own EFT insertion loop (per-
// processor-pair comm cost) with the contention multiplier forced off.
// Rather than re-derive new scenarios, this file reuses
// tests/fixtures/cheft.cpp's exact two scenarios and asserts the two
// properties that *define* pheft as an isolating control:
//
//   1. On cheft.cpp's pairwise-contention scenario (Scenario 1 there):
//      pheft must reproduce plain eheft's result exactly (task1 ->
//      proc1, makespan 8.0, NOT cheft's contention-corrected 12.0) --
//      proving pheft has no contention awareness, same as eheft.
//   2. On cheft.cpp's comm-cost scenario (Scenario 2a/2b there): pheft
//      must reproduce cheft's result exactly (directional per-pair
//      comm_cost(), NOT eheft's flat mean_comm_cost()) -- proving pheft
//      keeps cheft's comm model.
//
// Together these pin down that pheft sits exactly between eheft and
// cheft on one axis (comm model) while holding the other axis
// (contention-awareness) at eheft's "off" setting -- the whole point of
// using it as benchmark_suite.cpp's fourth scheduler: eheft->pheft error
// movement is attributable to the comm model alone, pheft->cheft error
// movement is attributable to contention-awareness alone.
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra -Isrc -o tests/fixtures/pheft
//         tests/fixtures/pheft.cpp src/dag/dag.cpp src/sched/heft.cpp
//         src/sched/eheft.cpp src/sched/pheft.cpp src/sched/cheft.cpp
//         src/profile/calibration.cpp
// Run:    ./tests/fixtures/pheft

#include "../../src/dag/dag.hpp"
#include "../../src/profile/calibration.hpp"
#include "../../src/sched/cheft.hpp"
#include "../../src/sched/eheft.hpp"
#include "../../src/sched/pheft.hpp"

#include <cmath>
#include <cstdio>
#include <string>

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

void check_close(double actual, double expected, double tol, const std::string& what) {
    ++g_checks;
    if (std::fabs(actual - expected) > tol) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s (expected %.4f, got %.4f)\n", what.c_str(), expected, actual);
    }
}

// --- Scenario 1 (pairwise contention), identical to cheft.cpp's ---

Dag build_topology() {
    Dag dag;

    Task t0;
    t0.id = 0;
    t0.kernel = KernelType::MEMORY_BOUND;
    t0.cost = {999.0, 999.0};
    dag.add_task(t0);

    Task t1;
    t1.id = 1;
    t1.kernel = KernelType::MEMORY_BOUND;
    t1.cost = {999.0, 999.0};
    dag.add_task(t1);

    Task t2;
    t2.id = 2;
    t2.kernel = KernelType::COMPUTE_BOUND;
    t2.cost = {999.0, 999.0};
    t2.preds = {0, 1};
    dag.add_task(t2);

    dag.add_edge(0, 2, 999.0);
    dag.add_edge(1, 2, 999.0);

    return dag;
}

profile::CalibrationProfile build_profile() {
    profile::CalibrationProfile p;
    p.num_processors = 2;
    p.sigma = {
        {4.0, 4.0},
        {4.0, 4.0},
    };
    p.pi = {{
        {1.0, 1.0},
        {1.0, 2.0},
    }};
    p.comm = {
        {0.0, 0.0},
        {0.0, 0.0},
    };
    p.all_pinned = true;
    return p;
}

// --- Scenario 2 (directional per-pair comm), identical to cheft.cpp's ---

Dag build_comm_topology(KernelType kernel_a, KernelType kernel_b) {
    Dag dag;

    Task t0;
    t0.id = 0;
    t0.kernel = kernel_a;
    t0.cost = {999.0, 999.0};
    dag.add_task(t0);

    Task t1;
    t1.id = 1;
    t1.kernel = kernel_b;
    t1.cost = {999.0, 999.0};
    t1.preds = {0};
    dag.add_task(t1);

    dag.add_edge(0, 1, 999.0);

    return dag;
}

profile::CalibrationProfile build_comm_profile() {
    profile::CalibrationProfile p;
    p.num_processors = 2;
    p.sigma = {
        {2.0, 100.0},
        {100.0, 2.0},
    };
    p.pi = {{
        {1.0, 1.0},
        {1.0, 1.0},
    }};
    p.comm = {
        {0.0, 1.0},
        {5.0, 0.0},
    };
    p.all_pinned = true;
    return p;
}

}  // namespace

int main() {
    // --- Property 1: pheft matches eheft (no contention correction) on
    // cheft.cpp's own pairwise-contention scenario, NOT cheft's
    // contention-corrected result. ---
    {
        Dag topology = build_topology();
        profile::CalibrationProfile profile = build_profile();

        HeftResult pheft_result = pheft_schedule(topology, profile);
        HeftResult eheft_result = eheft_schedule(topology, profile);
        HeftResult cheft_result = cheft_schedule(topology, profile);

        check(pheft_result.priority_order == eheft_result.priority_order,
              "pheft priority_order matches eheft (same calibrated-but-oblivious ranking)");

        check(pheft_result.schedule[1].processor == 1,
              "pheft: task1 -> processor 1 (naively parallel, same as eheft -- no contention awareness)");
        check_close(pheft_result.makespan, 8.0, 1e-9, "pheft makespan == 8.0 (uncorrected, matches eheft)");
        check_close(pheft_result.makespan, eheft_result.makespan, 1e-9, "pheft makespan == eheft makespan exactly");
        check(pheft_result.makespan != cheft_result.makespan,
              "pheft makespan differs from cheft's contention-corrected 12.0");

        PheftScheduler sched(profile);
        check(sched.name() == "pheft", "PheftScheduler::name() == \"pheft\"");
        Schedule adapted = sched.schedule(topology);
        check_close(adapted.makespan, 8.0, 1e-9, "PheftScheduler makespan matches pheft_schedule");
    }

    // --- Property 2: pheft matches cheft (per-pair comm), NOT eheft's
    // flat mean_comm_cost(), on cheft.cpp's own comm-cost scenario,
    // both directions. ---
    {
        Dag t = build_comm_topology(KernelType::COMPUTE_BOUND, KernelType::MEMORY_BOUND);
        profile::CalibrationProfile p = build_comm_profile();

        HeftResult pheft_result = pheft_schedule(t, p);
        HeftResult cheft_result = cheft_schedule(t, p);
        HeftResult eheft_result = eheft_schedule(t, p);

        check(pheft_result.schedule[0].processor == 0, "comm 2a: pheft task0 -> processor 0");
        check(pheft_result.schedule[1].processor == 1, "comm 2a: pheft task1 -> processor 1");
        check_close(pheft_result.schedule[1].start, 3.0, 1e-9,
                     "comm 2a: pheft task1 start reflects comm[0][1]=1.0 (2+1=3)");
        check_close(pheft_result.schedule[1].finish, 5.0, 1e-9, "comm 2a: pheft task1 finish == 5");
        check_close(pheft_result.makespan, cheft_result.makespan, 1e-9,
                     "comm 2a: pheft makespan matches cheft (both use per-pair comm)");
        check(pheft_result.makespan != eheft_result.makespan,
              "comm 2a: pheft makespan differs from eheft's flat-mean-comm result");
    }
    {
        Dag t = build_comm_topology(KernelType::MEMORY_BOUND, KernelType::COMPUTE_BOUND);
        profile::CalibrationProfile p = build_comm_profile();

        HeftResult pheft_result = pheft_schedule(t, p);
        HeftResult cheft_result = cheft_schedule(t, p);

        check(pheft_result.schedule[0].processor == 1, "comm 2b: pheft task0 -> processor 1");
        check(pheft_result.schedule[1].processor == 0, "comm 2b: pheft task1 -> processor 0");
        check_close(pheft_result.schedule[1].start, 7.0, 1e-9,
                     "comm 2b: pheft task1 start reflects comm[1][0]=5.0 (2+5=7)");
        check_close(pheft_result.schedule[1].finish, 9.0, 1e-9, "comm 2b: pheft task1 finish == 9");
        check_close(pheft_result.makespan, cheft_result.makespan, 1e-9,
                     "comm 2b: pheft makespan matches cheft (both use per-pair comm)");
    }

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
