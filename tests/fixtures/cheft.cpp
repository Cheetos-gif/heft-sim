// tests/fixtures/cheft.cpp
//
// Hand-verified regression test for src/sched/cheft.{hpp,cpp}'s
// contention-aware EFT insertion. Deterministic by construction (hand-set
// CalibrationProfiles, not a real measurement sweep) -- unlike a
// real-hardware comparison, this never depends on how much contention
// this particular machine happens to show, so it can be a hard
// pass/fail assertion instead of an observational print (see
// tests/fixtures/topcuoglu_calibrated.cpp, which prints eheft vs cheft
// side by side on *real* measured hardware and deliberately does not
// assert cheft beats eheft there).
//
// Three scenarios, each isolating one piece of cheft's model:
//
// --- Scenario 1: pairwise contention (build_topology/build_profile) ---
// Topology (3 tasks, 2 processors): task0 (MEMORY_BOUND, no preds),
// task1 (MEMORY_BOUND, no preds), task2 (COMPUTE_BOUND, preds={0,1}).
// sigma: COMPUTE=4, MEMORY=4 on every processor (uniform, so only pi
// drives any placement difference). pi[MEMORY][MEMORY]=2.0, every other
// pi entry=1.0. comm=0 everywhere (isolates contention from comm cost).
//
// rank_u (same calibrated-but-contention-oblivious ranking cheft and
// eheft both use): rank_u(2)=avg(4,4)=4; rank_u(0)=rank_u(1)=avg(4,4)+0+4=8.
// Priority order (ties broken by ascending id): 0, 1, 2.
//
// EFT insertion:
//   task0: proc0 finish=4, proc1 finish=4 (nothing placed yet) -- tie ->
//          proc0, [0,4].
//   task1: proc0 (sequential): start=4, no contention -> finish=8.
//          proc1 (parallel with task0): start=0; overlaps task0's
//          MEMORY run -> pi(MEMORY,MEMORY)=2.0 -> finish=0+4*2=8. Tie at
//          8 -> first evaluated (proc0) wins -> task1 -> proc0, [4,8]
//          (NOT the naively-parallel proc1 placement).
//   task2: ready=max(4,8)=8 (comm=0). proc0: start=8, no contention ->
//          finish=12. proc1: start=8, no contention -> finish=12. Tie ->
//          proc0 -> task2 -> proc0, [8,12].
// makespan = 12. Contrast: a plain eheft run on this exact profile
// places task1 on proc1 (predicted finish 4, ignoring the real
// pi(MEMORY,MEMORY)=2.0) and predicts makespan 8 -- checked below.
//
// --- Scenario 2: per-processor-pair comm cost, both directions
// (build_comm_topology/build_comm_profile_*) ---
// Two tasks of *different* kernels so sigma can force each onto a
// specific processor independently (sigma is indexed by kernel, not
// task identity): sigma[p0]={COMPUTE:2, MEMORY:100},
// sigma[p1]={COMPUTE:100, MEMORY:2}. pi=1.0 everywhere (isolates comm
// from contention). comm[0][1]=1.0, comm[1][0]=5.0 (asymmetric).
// Scenario 2a forces task0=COMPUTE (-> proc0) and task1=MEMORY,
// preds={0} (-> proc1 despite its own high p0 cost, because the
// alternative sequential-on-proc0 cost of 100 dwarfs any comm penalty);
// task1's predicted finish must reflect comm[0][1]=1.0 specifically (5,
// not 7 -- 7 is what mean_comm_cost()=3.0 would give, proving per-pair
// lookup, not the flat average eheft uses). Scenario 2b swaps which
// kernel is which (task0=MEMORY -> proc1, task1=COMPUTE, preds={0} ->
// proc0) to exercise the reverse direction: finish must reflect
// comm[1][0]=5.0 (9, not 5 -- 5 is what comm[0][1]=1.0 would wrongly
// give if direction were ignored).
//
// --- Scenario 3: N-way (3-way) contention (build_nway_topology/profile) ---
// 3 independent MEMORY_BOUND tasks, 3 processors, sigma MEMORY=4
// everywhere, pairwise pi=1.0 everywhere (isolates N-way from pairwise),
// pi3[MEMORY][{MEMORY,MEMORY}]=1.5 (all other pi3 entries=1.0). task0 ->
// proc0 [0,4], task1 -> proc1 [0,4] (both via the same no-contention-yet
// tie-break as scenario 1). task2's candidates: proc0/proc1 (sequential
// queues) both finish at 8; proc2 (parallel, overlapping BOTH task0 and
// task1 simultaneously -> exactly 2 co-runners, both MEMORY -> pi3
// combo, not pairwise) finishes at 0+4*1.5=6. 6 < 8, so task2 -> proc2,
// [0,6] -- the 3-way measurement must have been used: pairwise pi=1.0
// would wrongly give finish=4 (and the *old*, pre-pi3 max-pairwise
// algorithm would have picked proc2 at an incorrectly cheap 4, not the
// correct 6).
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

// --- Scenario 2: per-processor-pair comm cost, both directions ---

// task0=kernel_a (forced onto p0: cheap there, prohibitive on p1),
// task1=kernel_b, preds={0} (forced onto p1: cheap there, prohibitive
// on p0 -- so cheap that it's worth paying any plausible comm cost to
// get there).
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

    dag.add_edge(0, 1, 999.0);  // must be ignored -- calibrate_costs/cheft use profile.comm, not this

    return dag;
}

// sigma[p0]={COMPUTE:2, MEMORY:100}, sigma[p1]={COMPUTE:100, MEMORY:2}:
// whichever kernel a task is, one processor is cheap (2) and the other
// prohibitive (100) -- so sigma alone decides each task's processor,
// independent of any comm cost, letting this scenario isolate comm's
// effect on the *predicted finish time* instead of on placement choice.
// pi=1.0 everywhere (isolates comm from contention).
// comm[0][1]=1.0, comm[1][0]=5.0 -- deliberately asymmetric so a
// direction mix-up (or a flat mean_comm_cost()=3.0 substitution) would
// be caught by either scenario below.
profile::CalibrationProfile build_comm_profile() {
    profile::CalibrationProfile p;
    p.num_processors = 2;
    p.sigma = {
        {2.0, 100.0},  // processor 0: COMPUTE=2, MEMORY=100
        {100.0, 2.0},  // processor 1: COMPUTE=100, MEMORY=2
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

// --- Scenario 3: N-way (3-way) contention ---

Dag build_nway_topology() {
    Dag dag;
    for (int id = 0; id < 3; ++id) {
        Task t;
        t.id = id;
        t.kernel = KernelType::MEMORY_BOUND;
        t.cost = {999.0, 999.0, 999.0};
        dag.add_task(t);
    }
    return dag;
}

profile::CalibrationProfile build_nway_profile() {
    profile::CalibrationProfile p;
    p.num_processors = 3;
    p.sigma = {
        {2.0, 4.0},  // processor 0: COMPUTE=2 (unused here), MEMORY=4
        {2.0, 4.0},  // processor 1
        {2.0, 4.0},  // processor 2
    };
    p.pi = {{
        {1.0, 1.0},
        {1.0, 1.0},
    }};
    // pi3[kernel_index(reporter)][combo_index(a,b)]; combo_index maps
    // {COMPUTE,COMPUTE}->0, {COMPUTE,MEMORY}->1, {MEMORY,MEMORY}->2.
    p.pi3 = {{
        {1.0, 1.0, 1.0},  // COMPUTE reporter: unused here
        {1.0, 1.0, 1.5},  // MEMORY reporter, {MEMORY,MEMORY} co-runners: 1.5x
    }};
    p.comm.assign(3, std::vector<double>(3, 0.0));
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

    // --- Scenario 2a: comm[0][1]=1.0 used directionally (0 -> 1) ---
    {
        Dag t = build_comm_topology(KernelType::COMPUTE_BOUND, KernelType::MEMORY_BOUND);
        profile::CalibrationProfile p = build_comm_profile();
        HeftResult r = cheft_schedule(t, p);
        check(r.schedule[0].processor == 0, "comm 2a: task0 -> processor 0");
        check(r.schedule[1].processor == 1, "comm 2a: task1 -> processor 1");
        check_close(r.schedule[1].start, 3.0, 1e-9, "comm 2a: task1 start reflects comm[0][1]=1.0 (2+1=3)");
        check_close(r.schedule[1].finish, 5.0, 1e-9, "comm 2a: task1 finish == 5 (not 7 -- mean_comm_cost would give 7)");
    }

    // --- Scenario 2b: comm[1][0]=5.0 used directionally (1 -> 0) ---
    {
        Dag t = build_comm_topology(KernelType::MEMORY_BOUND, KernelType::COMPUTE_BOUND);
        profile::CalibrationProfile p = build_comm_profile();
        HeftResult r = cheft_schedule(t, p);
        check(r.schedule[0].processor == 1, "comm 2b: task0 -> processor 1");
        check(r.schedule[1].processor == 0, "comm 2b: task1 -> processor 0");
        check_close(r.schedule[1].start, 7.0, 1e-9, "comm 2b: task1 start reflects comm[1][0]=5.0 (2+5=7)");
        check_close(r.schedule[1].finish, 9.0, 1e-9, "comm 2b: task1 finish == 9 (not 5 -- the reverse direction's value)");
    }

    // --- Scenario 3: N-way (3-way) contention via pi3 ---
    {
        Dag t = build_nway_topology();
        profile::CalibrationProfile p = build_nway_profile();
        HeftResult r = cheft_schedule(t, p);
        check(r.schedule[0].processor == 0, "nway: task0 -> processor 0");
        check(r.schedule[1].processor == 1, "nway: task1 -> processor 1");
        check(r.schedule[2].processor == 2, "nway: task2 -> processor 2 (3-way-contended placement, correctly cheaper)");
        check_close(r.schedule[2].start, 0.0, 1e-9, "nway: task2 start");
        check_close(r.schedule[2].finish, 6.0, 1e-9,
                     "nway: task2 finish == 6 (pi3=1.5 used, not pairwise pi=1.0 which would give 4)");
        check_close(r.makespan, 6.0, 1e-9, "nway: makespan == 6");
    }

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
