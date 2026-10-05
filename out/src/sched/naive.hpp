// src/sched/naive.hpp
//
// Naive baseline scheduler: round-robin processor assignment, still
// respecting task dependencies. Contrast with HEFT (src/sched/heft.hpp),
// which ranks tasks by upward rank and picks whichever processor
// minimizes earliest finish time, inserting into idle gaps. Naive does
// neither:
//   - Task order is a plain topological sort (Kahn's algorithm, ties
//     broken by ascending task id) -- just enough ordering to know a
//     predecessor's predicted finish time before its successor is
//     scheduled. No priority/rank is computed.
//   - Processor choice cycles 0, 1, ..., P-1, 0, 1, ... in that
//     topological order, ignoring cost and load entirely.
//   - Each task is appended after the last task already scheduled on its
//     processor (or after its predecessors' data arrives, whichever is
//     later) -- never inserted into an earlier idle gap.
//
// Like heft.hpp, this only predicts a schedule; it never touches threads.

#pragma once

#include "../dag/dag.hpp"
#include "scheduler.hpp"

#include <string>
#include <vector>

namespace heftsim {

struct NaiveResult {
    std::vector<ScheduleEntry> schedule;  // indexed by task id: schedule[i].task == i
    std::vector<int> order;               // task ids in topological (assignment) order
    double makespan = 0.0;
};

// A topological order of dag's tasks, ties broken by ascending task id.
// Throws std::runtime_error if dag contains a cycle (Kahn's algorithm
// terminates with fewer than num_tasks() entries in that case).
std::vector<int> topological_order(const Dag& dag);

// Runs the naive scheduler end to end. Returns the predicted schedule and
// makespan.
NaiveResult naive_schedule(const Dag& dag);

// Scheduler-interface adapter (src/sched/scheduler.hpp) over
// naive_schedule(); drops the topological `order` extra that only
// naive_schedule()'s own callers need.
class NaiveScheduler : public Scheduler {
public:
    std::string name() const override { return "naive"; }
    Schedule schedule(const Dag& dag) const override {
        NaiveResult r = naive_schedule(dag);
        return Schedule{std::move(r.schedule), r.makespan};
    }
};

}  // namespace heftsim
