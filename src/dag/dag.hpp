// src/dag/dag.hpp
//
// A directed acyclic task graph: tasks carrying per-processor cost, wired
// together by weighted edges carrying inter-task communication cost.
// Construction is deliberately strict (throws on malformed input) since a
// bad fixture should fail loudly, not silently mis-schedule.

#pragma once

#include "task.hpp"

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace heftsim {

class Dag {
public:
    // Appends a task. task.id must equal its position in insertion order
    // (0, 1, 2, ...). task.preds must reference only already-added task
    // ids. Every task must carry the same number of per-processor costs.
    // Throws std::invalid_argument on any violation.
    void add_task(Task task);

    // Adds a directed edge from -> to carrying comm_cost (the time to move
    // from's output to to's processor, when they land on different
    // processors; same-processor transfers cost 0 in EST/EFT math done by
    // the scheduler). `to` must already list `from` among task(to).preds
    // (set when `to` was added). Throws std::invalid_argument otherwise.
    void add_edge(int from, int to, double comm_cost);

    const Task& task(int id) const;
    std::size_t num_tasks() const noexcept { return tasks_.size(); }
    std::size_t num_processors() const noexcept { return num_processors_; }

    const std::vector<int>& successors(int id) const;

    // Communication cost of edge from -> to. 0 if no such edge was added
    // (including the case from == to).
    double comm_cost(int from, int to) const;

    std::vector<int> entry_tasks() const;  // tasks with no predecessors
    std::vector<int> exit_tasks() const;   // tasks with no successors

private:
    std::vector<Task> tasks_;
    std::vector<std::vector<int>> succs_;
    std::map<std::pair<int, int>, double> edge_cost_;
    std::size_t num_processors_ = 0;
};

}  // namespace heftsim
