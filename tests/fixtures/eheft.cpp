// tests/fixtures/eheft.cpp
//
// Hand-verified regression test for src/sched/eheft.{hpp,cpp}.
//
// Can't reuse tests/fixtures/topcuoglu_dag.hpp's paper fixture the way
// tests/fixtures/topcuoglu.cpp does: eheft substitutes Task::cost from a
// CalibrationProfile indexed only by (kernel, processor) -- two kernel
// classes -- while the paper's Table 1 gives each of its ten tasks its
// own distinct per-processor cost. No two-kernel-class CalibrationProfile
// can reproduce that table, so this file uses its own small, fully
// hand-computed fixture instead.
//
// Topology (3 tasks, 2 processors):
//   task0 (COMPUTE_BOUND), no preds
//   task1 (MEMORY_BOUND), preds={0}, edge 0->1 cost 999 (must be zeroed)
//   task2 (COMPUTE_BOUND), preds={1}, edge 1->2 cost 999 (must be zeroed)
//
// CalibrationProfile (hand-set, not measured):
//   processor0: COMPUTE=2.0  MEMORY=5.0
//   processor1: COMPUTE=3.0  MEMORY=4.0
//   comm[0][1] = comm[1][0] = 0.5  =>  mean_comm_cost() = 0.5
//
// Expected calibrated costs: task0=[2,3], task1=[5,4], task2=[2,3];
// every edge's comm_cost becomes 0.5 (mean_comm_cost(), not the
// topology's original 999 -- see src/sched/eheft.hpp::calibrate_costs).
// Expected rank_u (comm cost is unconditional in rank_u, same formula
// regardless of eventual placement -- src/sched/heft.cpp's rank_u_of):
//   rank_u(2) = avg(2,3)                = 2.5
//   rank_u(1) = avg(5,4) + 0.5 + 2.5     = 7.5
//   rank_u(0) = avg(2,3) + 0.5 + 7.5     = 10.5
// Priority order: 0 (10.5), 1 (7.5), 2 (2.5).
// EFT insertion (comm only charged when pred/candidate processors
// differ -- src/sched/heft.cpp's EFT loop):
//   task0: EFT(p0)=0+2=2.0, EFT(p1)=0+3=3.0 -> p0, [0,2]
//   task1: candidate p0 (same proc as task0): ready=2.0+0=2.0;
//          EFT(p0)=max(2,2)+5=7.0 (p0 busy till 2)
//          candidate p1 (different proc): ready=2.0+0.5=2.5;
//          EFT(p1)=max(0,2.5)+4=6.5 -> p1, [2.5,6.5]
//   task2: candidate p0 (different from task1's p1): ready=6.5+0.5=7.0;
//          EFT(p0)=max(2,7.0)+2=9.0 (p0 idle after 2)
//          candidate p1 (same proc as task1): ready=6.5+0=6.5;
//          EFT(p1)=max(6.5,6.5)+3=9.5 -> p0, [7.0,9.0]
// makespan = 9.0
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra -Isrc -o tests/fixtures/eheft
//         tests/fixtures/eheft.cpp src/dag/dag.cpp src/sched/heft.cpp
//         src/sched/eheft.cpp
// Run:    ./tests/fixtures/eheft

#include "../../src/dag/dag.hpp"
#include "../../src/profile/calibration.hpp"
#include "../../src/sched/eheft.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>
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
    t0.kernel = KernelType::COMPUTE_BOUND;
    t0.cost = {999.0, 999.0};  // must be ignored by calibrate_costs
    dag.add_task(t0);

    Task t1;
    t1.id = 1;
    t1.kernel = KernelType::MEMORY_BOUND;
    t1.cost = {999.0, 999.0};
    t1.preds = {0};
    dag.add_task(t1);

    Task t2;
    t2.id = 2;
    t2.kernel = KernelType::COMPUTE_BOUND;
    t2.cost = {999.0, 999.0};
    t2.preds = {1};
    dag.add_task(t2);

    dag.add_edge(0, 1, 999.0);  // must become 0 after calibration
    dag.add_edge(1, 2, 999.0);

    return dag;
}

profile::CalibrationProfile build_profile() {
    profile::CalibrationProfile p;
    p.num_processors = 2;
    p.sigma = {
        {2.0, 5.0},  // processor 0: COMPUTE=2, MEMORY=5
        {3.0, 4.0},  // processor 1: COMPUTE=3, MEMORY=4
    };
    p.pi = {{{1.0, 1.0}, {1.0, 1.0}}};
    p.comm = {
        {0.0, 0.5},  // comm[0][0]=0 (same processor), comm[0][1]=0.5
        {0.5, 0.0},  // comm[1][0]=0.5, comm[1][1]=0 (same processor)
    };
    p.all_pinned = true;
    return p;
}

}  // namespace

int main() {
    Dag topology = build_topology();
    profile::CalibrationProfile profile = build_profile();

    // --- calibrate_costs: substitution and zeroed comm cost ---
    Dag calibrated = calibrate_costs(topology, profile);
    check_close(calibrated.task(0).cost[0], 2.0, 1e-9, "task0.cost[p0] == sigma(COMPUTE,0)");
    check_close(calibrated.task(0).cost[1], 3.0, 1e-9, "task0.cost[p1] == sigma(COMPUTE,1)");
    check_close(calibrated.task(1).cost[0], 5.0, 1e-9, "task1.cost[p0] == sigma(MEMORY,0)");
    check_close(calibrated.task(1).cost[1], 4.0, 1e-9, "task1.cost[p1] == sigma(MEMORY,1)");
    check_close(calibrated.task(2).cost[0], 2.0, 1e-9, "task2.cost[p0] == sigma(COMPUTE,0)");
    check_close(calibrated.task(2).cost[1], 3.0, 1e-9, "task2.cost[p1] == sigma(COMPUTE,1)");
    check_close(calibrated.comm_cost(0, 1), 0.5, 1e-9, "edge 0->1 becomes mean_comm_cost()");
    check_close(calibrated.comm_cost(1, 2), 0.5, 1e-9, "edge 1->2 becomes mean_comm_cost()");

    // --- calibrate_costs: processor-count mismatch throws ---
    profile::CalibrationProfile bad_profile = build_profile();
    bad_profile.num_processors = 3;
    bad_profile.sigma.push_back({1.0, 1.0});
    bool threw = false;
    try {
        calibrate_costs(topology, bad_profile);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "calibrate_costs: processor-count mismatch throws std::invalid_argument");

    // --- eheft_schedule: hand-computed rank_u / schedule / makespan ---
    HeftResult result = eheft_schedule(topology, profile);

    check_close(result.rank_u[2], 2.5, 1e-9, "rank_u(task2)");
    check_close(result.rank_u[1], 7.5, 1e-9, "rank_u(task1)");
    check_close(result.rank_u[0], 10.5, 1e-9, "rank_u(task0)");

    check(result.priority_order == std::vector<int>({0, 1, 2}), "priority_order == [0,1,2]");

    check(result.schedule[0].processor == 0, "task0 -> processor 0");
    check_close(result.schedule[0].start, 0.0, 1e-9, "task0 start");
    check_close(result.schedule[0].finish, 2.0, 1e-9, "task0 finish");

    check(result.schedule[1].processor == 1, "task1 -> processor 1");
    check_close(result.schedule[1].start, 2.5, 1e-9, "task1 start");
    check_close(result.schedule[1].finish, 6.5, 1e-9, "task1 finish");

    check(result.schedule[2].processor == 0, "task2 -> processor 0");
    check_close(result.schedule[2].start, 7.0, 1e-9, "task2 start");
    check_close(result.schedule[2].finish, 9.0, 1e-9, "task2 finish");

    check_close(result.makespan, 9.0, 1e-9, "makespan");

    // --- EheftScheduler adapter: matches eheft_schedule() ---
    EheftScheduler sched(profile);
    check(sched.name() == "eheft", "EheftScheduler::name() == \"eheft\"");
    Schedule adapted = sched.schedule(topology);
    check_close(adapted.makespan, 9.0, 1e-9, "EheftScheduler makespan matches eheft_schedule");

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
