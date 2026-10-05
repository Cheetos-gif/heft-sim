// tests/fixtures/topcuoglu_dag.hpp
//
// Shared fixture: the DAG, per-processor cost table, and edges from
// H. Topcuoglu, S. Hariri, M.-Y. Wu, "Performance-Effective and
// Low-Complexity Task Scheduling for Heterogeneous Computing," IEEE TPDS
// 13(3), 2002 -- Fig. 1 (task graph) and Table 1 (computation costs).
//
// Tasks are 0-indexed here (paper's n1..n10 -> task 0..9); processors are
// 0-indexed (paper's P1..P3 -> processor 0..2). Used by both
// tests/fixtures/topcuoglu.cpp (HEFT rank/schedule regression) and
// tests/fixtures/topcuoglu_exec.cpp (end-to-end execution demo) so the two
// never drift apart.

#pragma once

#include "../../src/dag/dag.hpp"

#include <vector>

namespace heftsim::fixtures {

// Builds the paper's Fig. 1 / Table 1 DAG.
//
// Table 1 (computation cost, task x {P1,P2,P3}):
//   n1: 14 16  9      n2: 13 19 18      n3: 11 13 19      n4: 13  8 17
//   n5: 12 13 10      n6: 13 16  9      n7:  7 15 11      n8:  5 11 14
//   n9: 18 12 20      n10: 21  7 16
//
// Fig. 1 edges (communication cost):
//   n1->n2:18  n1->n3:12  n1->n4:9  n1->n5:11  n1->n6:14
//   n2->n8:19  n2->n9:16
//   n3->n7:23
//   n4->n8:27  n4->n9:23
//   n5->n9:13
//   n6->n8:15
//   n7->n10:17
//   n8->n10:11
//   n9->n10:13
inline Dag build_topcuoglu_dag() {
    Dag dag;

    auto add = [&](int id, std::vector<double> cost, std::vector<int> preds) {
        Task t;
        t.id = id;
        t.cost = std::move(cost);
        t.preds = std::move(preds);
        // The paper has no notion of kernel type; alternate as an
        // illustrative tag for this project's Task struct only.
        t.kernel = (id % 2 == 0) ? KernelType::COMPUTE_BOUND : KernelType::MEMORY_BOUND;
        dag.add_task(std::move(t));
    };

    add(0, {14, 16, 9}, {});
    add(1, {13, 19, 18}, {0});
    add(2, {11, 13, 19}, {0});
    add(3, {13, 8, 17}, {0});
    add(4, {12, 13, 10}, {0});
    add(5, {13, 16, 9}, {0});
    add(6, {7, 15, 11}, {2});
    add(7, {5, 11, 14}, {1, 3, 5});
    add(8, {18, 12, 20}, {1, 3, 4});
    add(9, {21, 7, 16}, {6, 7, 8});

    dag.add_edge(0, 1, 18);
    dag.add_edge(0, 2, 12);
    dag.add_edge(0, 3, 9);
    dag.add_edge(0, 4, 11);
    dag.add_edge(0, 5, 14);
    dag.add_edge(1, 7, 19);
    dag.add_edge(1, 8, 16);
    dag.add_edge(2, 6, 23);
    dag.add_edge(3, 7, 27);
    dag.add_edge(3, 8, 23);
    dag.add_edge(4, 8, 13);
    dag.add_edge(5, 7, 15);
    dag.add_edge(6, 9, 17);
    dag.add_edge(7, 9, 11);
    dag.add_edge(8, 9, 13);

    return dag;
}

}  // namespace heftsim::fixtures
