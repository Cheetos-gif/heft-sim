// src/sched/scheduler.hpp
//
// The one interface every scheduler in src/sched/ implements (CLAUDE.md
// conventions: "Schedulers implement one interface; they are
// interchangeable" and "No scheduler touches threads. No executor makes
// placement decisions."). A Scheduler only predicts: given a Dag, it
// returns a processor assignment and timing for every task, plus a
// predicted makespan, computed before any thread runs.

#pragma once

#include "../dag/dag.hpp"

#include <string>
#include <vector>

namespace heftsim {

struct ScheduleEntry {
    int task = -1;
    int processor = -1;
    double start = 0.0;
    double finish = 0.0;
};

// Common output shape every scheduler produces. Algorithm-specific extras
// (HEFT's rank_u and priority_order, for instance) live in that
// algorithm's own result struct, which is a superset of this shape --
// src/exec/dispatcher.hpp only ever needs `schedule` and `makespan`.
struct Schedule {
    std::vector<ScheduleEntry> schedule;  // indexed by task id: schedule[i].task == i
    double makespan = 0.0;
};

class Scheduler {
public:
    virtual ~Scheduler() = default;
    virtual std::string name() const = 0;
    virtual Schedule schedule(const Dag& dag) const = 0;
};

}  // namespace heftsim
