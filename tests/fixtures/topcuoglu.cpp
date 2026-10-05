// tests/fixtures/topcuoglu.cpp
//
// Reference-example regression test for src/dag and src/sched/heft.
//
// The DAG, per-processor cost table, upward ranks, and final schedule are
// all taken from H. Topcuoglu, S. Hariri, M.-Y. Wu, "Performance-Effective
// and Low-Complexity Task Scheduling for Heterogeneous Computing," IEEE
// TPDS 13(3), 2002 -- Fig. 1 (task graph) and Table 1 (computation costs).
// rank_u values are the paper's own worked numbers (Table 2); the final
// schedule and makespan=80 are the paper's Fig. 6/Table 3 result.
//
// Tasks are 0-indexed here (paper's n1..n10 -> task 0..9); processors are
// 0-indexed (paper's P1..P3 -> processor 0..2).
//
// If any assertion below fails, the implementation disagrees with the
// published paper -- fix src/dag or src/sched/heft, never this file.
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra -Isrc -o tests/fixtures/topcuoglu
//         tests/fixtures/topcuoglu.cpp src/dag/dag.cpp src/sched/heft.cpp
// Run:    ./tests/fixtures/topcuoglu

#include "../../src/dag/dag.hpp"
#include "../../src/sched/heft.hpp"
#include "topcuoglu_dag.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using heftsim::Dag;
using heftsim::HeftResult;
using heftsim::fixtures::build_topcuoglu_dag;

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

void test_dag_shape(const Dag& dag) {
    check(dag.num_tasks() == 10, "10 tasks");
    check(dag.num_processors() == 3, "3 processors");
    check(dag.entry_tasks() == std::vector<int>{0}, "task 0 is the sole entry task");
    check(dag.exit_tasks() == std::vector<int>{9}, "task 9 is the sole exit task");
}

void test_upward_ranks(const std::vector<double>& rank_u) {
    // Table 2 in the paper.
    const std::vector<double> expected = {108.0,     77.0,      80.0,      80.0, 69.0,
                                           63.333,    42.667,    35.667,    44.333, 14.667};
    for (int i = 0; i < 10; ++i) {
        check_close(rank_u[i], expected[i], 0.001, "rank_u[" + std::to_string(i) + "]");
    }
}

void test_priority_order(const std::vector<int>& order) {
    // Decreasing rank_u; the only tie is n3/n4 (both 80.0), broken by
    // ascending id as the paper's own worked trace does (n3 before n4).
    const std::vector<int> expected = {0, 2, 3, 1, 4, 5, 8, 6, 7, 9};
    check(order == expected, "priority order == paper's decreasing-rank_u trace");
}

void test_schedule(const HeftResult& result) {
    // Fig. 6 / Table 3 in the paper: processor is 0-indexed here (P1->0
    // etc.), start/finish are absolute time units.
    struct Expected {
        int proc;
        double start, finish;
    };
    const std::vector<Expected> expected = {
        /*0*/ {2, 0, 9},
        /*1*/ {0, 27, 40},
        /*2*/ {2, 9, 28},
        /*3*/ {1, 18, 26},
        /*4*/ {2, 28, 38},
        /*5*/ {1, 26, 42},
        /*6*/ {2, 38, 49},
        /*7*/ {0, 57, 62},
        /*8*/ {1, 56, 68},
        /*9*/ {1, 73, 80},
    };
    check(result.schedule.size() == expected.size(), "schedule has 10 entries");
    for (std::size_t i = 0; i < expected.size() && i < result.schedule.size(); ++i) {
        const auto& e = result.schedule[i];
        const auto& x = expected[i];
        check(e.task == static_cast<int>(i), "schedule[" + std::to_string(i) + "].task id");
        check(e.processor == x.proc, "task " + std::to_string(i) + " processor");
        check_close(e.start, x.start, 1e-9, "task " + std::to_string(i) + " start");
        check_close(e.finish, x.finish, 1e-9, "task " + std::to_string(i) + " finish");
    }
    check_close(result.makespan, 80.0, 1e-9, "predicted makespan == 80 (paper's published result)");
}

}  // namespace

int main() {
    Dag dag = build_topcuoglu_dag();
    test_dag_shape(dag);

    std::vector<double> rank_u = heftsim::compute_upward_ranks(dag);
    test_upward_ranks(rank_u);

    HeftResult result = heftsim::heft_schedule(dag);
    test_priority_order(result.priority_order);
    test_schedule(result);

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
