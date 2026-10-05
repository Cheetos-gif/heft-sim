// tests/fixtures/naive.cpp
//
// Regression test for src/sched/naive on the Topcuoglu fixture DAG
// (tests/fixtures/topcuoglu_dag.hpp -- same DAG the HEFT test uses, so the
// two schedulers are directly comparable).
//
// Expected numbers are this project's own specification of "naive":
// topological order (Kahn's algorithm, ties broken by ascending id),
// processor = position-in-order % num_processors, append-only EST/EFT
// (never inserted into an idle gap). Independently hand- and
// script-verified against src/sched/naive.cpp's algorithm description
// before being pinned here -- see the worked table below.
//
// Because task ids in this fixture were assigned in a dependency-order-
// consistent way (every predecessor has a smaller id than its
// successors), the topological order happens to be exactly 0..9.
//
//   task  proc  start   finish   (P0/P1/P2 costs: see topcuoglu_dag.hpp)
//      0     0    0.0     14.0
//      1     1   32.0     51.0
//      2     2   26.0     45.0
//      3     0   14.0     27.0
//      4     1   51.0     64.0
//      5     2   45.0     54.0
//      6     0   68.0     75.0
//      7     1   69.0     80.0
//      8     2   77.0     97.0
//      9     0  110.0    131.0
//   makespan: 131.0
//
// If any assertion below fails, the implementation disagrees with this
// specification -- fix src/sched/naive.cpp, never this file, unless the
// worked table itself is re-derived and shown to be wrong.
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra -Isrc -o tests/fixtures/naive
//         tests/fixtures/naive.cpp src/dag/dag.cpp src/sched/naive.cpp
// Run:    ./tests/fixtures/naive

#include "../../src/dag/dag.hpp"
#include "../../src/sched/naive.hpp"
#include "topcuoglu_dag.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using heftsim::Dag;
using heftsim::NaiveResult;
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

struct Expected {
    int proc;
    double start, finish;
};

}  // namespace

int main() {
    Dag dag = build_topcuoglu_dag();

    std::vector<int> order = heftsim::topological_order(dag);
    const std::vector<int> expected_order = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    check(order == expected_order, "topological order == 0..9 for this fixture");

    NaiveResult result = heftsim::naive_schedule(dag);
    check(result.order == expected_order, "NaiveResult.order matches topological_order()");

    const std::vector<Expected> expected = {
        /*0*/ {0, 0.0, 14.0},
        /*1*/ {1, 32.0, 51.0},
        /*2*/ {2, 26.0, 45.0},
        /*3*/ {0, 14.0, 27.0},
        /*4*/ {1, 51.0, 64.0},
        /*5*/ {2, 45.0, 54.0},
        /*6*/ {0, 68.0, 75.0},
        /*7*/ {1, 69.0, 80.0},
        /*8*/ {2, 77.0, 97.0},
        /*9*/ {0, 110.0, 131.0},
    };
    check(result.schedule.size() == expected.size(), "schedule has 10 entries");
    for (std::size_t i = 0; i < expected.size() && i < result.schedule.size(); ++i) {
        const auto& e = result.schedule[i];
        const auto& x = expected[i];
        check(e.task == static_cast<int>(i), "schedule[" + std::to_string(i) + "].task id");
        check(e.processor == x.proc, "task " + std::to_string(i) + " processor (round-robin i%3)");
        check_close(e.start, x.start, 1e-9, "task " + std::to_string(i) + " start");
        check_close(e.finish, x.finish, 1e-9, "task " + std::to_string(i) + " finish");
    }
    check_close(result.makespan, 131.0, 1e-9, "predicted naive makespan == 131 (worse than HEFT's 80)");

    // A structural property naive must hold, independent of the exact
    // numbers above: strictly respects precedence (each task's predicted
    // start is at or after every predecessor's predicted finish).
    for (const auto& e : result.schedule) {
        for (int pred : dag.task(e.task).preds) {
            check(e.start + 1e-9 >= result.schedule[pred].finish,
                  "task " + std::to_string(e.task) + " predicted start respects predecessor " + std::to_string(pred));
        }
    }

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
