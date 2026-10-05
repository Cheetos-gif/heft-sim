// tests/fixtures/cheft.cpp
//
// Hand-verified regression test for src/sched/cheft.{hpp,cpp}'s
// contention-aware EFT insertion. Deterministic by construction (a
// hand-set CalibrationProfile, not a real measurement sweep) -- unlike a
// real-hardware comparison, this never depends on how much contention
// this particular machine happens to show, so it can be a hard
// pass/fail assertion instead of an observational print (see
// tests/fixtures/topcuoglu_calibrated.cpp, which prints eheft vs cheft
// side by side on *real* measured hardware and deliberately does not
// assert cheft beats eheft there -- real contention magnitude is
// workload/hardware dependent, exactly the thing this project measures,
// not something a committed test can guarantee without being flaky).
//
// Topology (3 tasks, 2 processors):
//   task0 (MEMORY_BOUND), no preds
//   task1 (MEMORY_BOUND), no preds
//   task2 (COMPUTE_BOUND), preds={0,1}
// task0 and task1 are independent -- nothing stops HEFT-style EFT from
// placing them concurrently on different processors.
//
// CalibrationProfile (hand-set):
//   sigma: COMPUTE=4, MEMORY=4 on every processor (deliberately uniform
//   so only pi, not sigma, drives any placement difference).
//   pi[MEMORY][MEMORY] = 2.0 (heavy self-interference), every other pi
//   entry = 1.0 (no interference). comm = 0 everywhere (isolates the
//   contention effect from any communication-cost effect).
//
// rank_u (same calibrated-but-contention-oblivious ranking cheft and
// eheft both use -- see cheft.hpp on why rank_u can't be contention-
// aware):
//   rank_u(2) = avg(4,4)            = 4
//   rank_u(0) = avg(4,4) + 0 + 4     = 8
//   rank_u(1) = avg(4,4) + 0 + 4     = 8
// Priority order (ties broken by ascending id): 0, 1, 2.
//
// EFT insertion:
//   task0: proc0 finish=4, proc1 finish=4 (nothing placed yet on either
//          -> no contention either way) -- tie -> proc0, [0,4].
//   task1: proc0 (sequential after task0): start=4, no contention
//          (proc1 empty) -> finish=8.
//          proc1 (parallel with task0): start=0 (base duration 4);
//          overlaps task0's MEMORY run on proc0 -> pi(MEMORY,MEMORY)=2.0
//          -> finish=0+4*2=8.
//          Tie at 8 -> first evaluated (proc0) wins -> task1 -> proc0,
//          [4,8] (NOT the naively-parallel proc1 placement).
//   task2: ready=max(4,8)=8 (comm=0). proc0: start=8 (after task0,task1),
//          no contention (proc1 empty) -> finish=12. proc1: start=8, no
//          contention (proc0's entries don't overlap [8,12)) -> finish=12.
//          Tie -> proc0 -> task2 -> proc0, [8,12].
// makespan = 12.
//
// Contrast: a plain (contention-oblivious) eheft run on this exact
// profile places task1 on proc1 (predicted finish 4, ignoring the real
// pi(MEMORY,MEMORY)=2.0 contention) and predicts makespan 8 -- see the
// EheftScheduler comparison check below. cheft's 12 is the one that
// would match reality if this machine's real MEMORY/MEMORY contention
// genuinely were 2x.
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra -Isrc -o tests/fixtures/cheft
//         tests/fixtures/cheft.cpp src/dag/dag.cpp src/sched/heft.cpp
//         src/sched/eheft.cpp src/sched/cheft.cpp src/profile/calibration.cpp
// Run:    ./tests/fixtures/cheft

#include "../../src/dag/dag.hpp"
#include "../../src/profile/calibration.hpp"
#include "../../src/sched/cheft.hpp"
#include "../../src/sched/eheft.hpp"

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

Dag build_topology() {
    Dag dag;

    Task t0;
    t0.id = 0;
    t0.kernel = KernelType::MEMORY_BOUND;
    t0.cost = {999.0, 999.0};  // must be ignored -- calibrate_costs substitutes sigma
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

    dag.add_edge(0, 2, 999.0);  // must become 0 (mean_comm_cost)
    dag.add_edge(1, 2, 999.0);

    return dag;
}

profile::CalibrationProfile build_profile() {
    profile::CalibrationProfile p;
    p.num_processors = 2;
    p.sigma = {
        {4.0, 4.0},  // processor 0: COMPUTE=4, MEMORY=4
        {4.0, 4.0},  // processor 1: COMPUTE=4, MEMORY=4
    };
    // pi[kernel_index(reporter)][kernel_index(interferer)];
    // kernel_index(COMPUTE_BOUND)=0, kernel_index(MEMORY_BOUND)=1.
    p.pi = {{
        {1.0, 1.0},  // COMPUTE reporter: unaffected by either interferer
        {1.0, 2.0},  // MEMORY reporter: unaffected by COMPUTE, 2x by MEMORY
    }};
    p.comm = {
        {0.0, 0.0},
        {0.0, 0.0},
    };
    p.all_pinned = true;
    return p;
}

}  // namespace

int main() {
    Dag topology = build_topology();
    profile::CalibrationProfile profile = build_profile();

    // --- cheft_schedule: hand-computed rank_u / schedule / makespan ---
    HeftResult cheft_result = cheft_schedule(topology, profile);

    check_close(cheft_result.rank_u[2], 4.0, 1e-9, "cheft rank_u(task2)");
    check_close(cheft_result.rank_u[0], 8.0, 1e-9, "cheft rank_u(task0)");
    check_close(cheft_result.rank_u[1], 8.0, 1e-9, "cheft rank_u(task1)");
    check(cheft_result.priority_order == std::vector<int>({0, 1, 2}), "cheft priority_order == [0,1,2]");

    check(cheft_result.schedule[0].processor == 0, "cheft: task0 -> processor 0");
    check_close(cheft_result.schedule[0].start, 0.0, 1e-9, "cheft: task0 start");
    check_close(cheft_result.schedule[0].finish, 4.0, 1e-9, "cheft: task0 finish");

    check(cheft_result.schedule[1].processor == 0, "cheft: task1 -> processor 0 (avoids contended proc1)");
    check_close(cheft_result.schedule[1].start, 4.0, 1e-9, "cheft: task1 start");
    check_close(cheft_result.schedule[1].finish, 8.0, 1e-9, "cheft: task1 finish");

    check(cheft_result.schedule[2].processor == 0, "cheft: task2 -> processor 0");
    check_close(cheft_result.schedule[2].start, 8.0, 1e-9, "cheft: task2 start");
    check_close(cheft_result.schedule[2].finish, 12.0, 1e-9, "cheft: task2 finish");

    check_close(cheft_result.makespan, 12.0, 1e-9, "cheft makespan == 12 (contention-corrected)");

    // --- Contrast: plain eheft on the identical profile ignores the
    // real pi(MEMORY,MEMORY)=2.0 contention and places task1 on proc1,
    // underpredicting makespan at 8 instead of 12. This is the exact
    // scenario cheft exists to fix. ---
    HeftResult eheft_result = eheft_schedule(topology, profile);
    check(eheft_result.schedule[1].processor == 1, "eheft (uncorrected): task1 -> processor 1 (naively parallel)");
    check_close(eheft_result.makespan, 8.0, 1e-9, "eheft (uncorrected) makespan == 8 (ignores contention)");
    check(cheft_result.makespan > eheft_result.makespan,
          "cheft's contention-corrected makespan prediction exceeds eheft's uncorrected one on this scenario");

    // --- CheftScheduler adapter: matches cheft_schedule() ---
    CheftScheduler sched(profile);
    check(sched.name() == "cheft", "CheftScheduler::name() == \"cheft\"");
    Schedule adapted = sched.schedule(topology);
    check_close(adapted.makespan, 12.0, 1e-9, "CheftScheduler makespan matches cheft_schedule");

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
