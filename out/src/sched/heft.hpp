// src/sched/heft.hpp
//
// HEFT: Heterogeneous Earliest Finish Time (Topcuoglu, Hariri, Wu, 2002).
//
// Two phases:
//   1. Task prioritizing: every task gets an upward rank, rank_u(v) =
//      avg_cost(v) + max over successors s of (comm_cost(v,s) + rank_u(s)),
//      with rank_u(exit) = avg_cost(exit). Computed recursively, memoized.
//   2. Processor selection: tasks are visited in decreasing rank_u order
//      (ties broken by ascending task id for determinism) and each is
//      inserted -- into an idle slot on whichever processor gives the
//      earliest finish time (EFT), not necessarily appended at the end of
//      that processor's timeline.
//
// This header only predicts a schedule; it never touches threads. See
// CLAUDE.md: no scheduler makes placement decisions on real hardware, and
// every scheduler must emit a predicted makespan before execution.

#pragma once

#include "../dag/dag.hpp"
#include "scheduler.hpp"

#include <string>
#include <vector>

namespace heftsim {

struct HeftResult {
    std::vector<ScheduleEntry> schedule;  // indexed by task id: schedule[i].task == i
    std::vector<double> rank_u;           // indexed by task id
    std::vector<int> priority_order;      // task ids in the order they were assigned
    double makespan = 0.0;
};

// Upward rank of every task in `dag`, indexed by task id. Recursive,
// memoized bottom-up from exit tasks. Throws std::runtime_error if `dag`
// contains a cycle.
std::vector<double> compute_upward_ranks(const Dag& dag);

// Runs HEFT end to end: ranks, orders, inserts. Returns the predicted
// schedule and makespan.
HeftResult heft_schedule(const Dag& dag);

// Scheduler-interface adapter (src/sched/scheduler.hpp) over
// heft_schedule(); drops the HEFT-specific rank_u/priority_order extras
// that only heft_schedule()'s own callers need.
class HeftScheduler : public Scheduler {
public:
    std::string name() const override { return "HEFT"; }
    Schedule schedule(const Dag& dag) const override {
        HeftResult r = heft_schedule(dag);
        return Schedule{std::move(r.schedule), r.makespan};
    }
};

}  // namespace heftsim
