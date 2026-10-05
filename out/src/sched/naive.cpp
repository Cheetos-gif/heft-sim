// src/sched/naive.cpp
#include "naive.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <vector>

namespace heftsim {

std::vector<int> topological_order(const Dag& dag) {
    const std::size_t n = dag.num_tasks();

    std::vector<int> indegree(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        indegree[i] = static_cast<int>(dag.task(static_cast<int>(i)).preds.size());
    }

    // Min-heap over ready task ids so ties (multiple tasks ready at once)
    // resolve to ascending id, deterministically -- same convention as
    // HEFT's rank tie-break.
    std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
    for (std::size_t i = 0; i < n; ++i) {
        if (indegree[i] == 0) ready.push(static_cast<int>(i));
    }

    std::vector<int> order;
    order.reserve(n);
    while (!ready.empty()) {
        int t = ready.top();
        ready.pop();
        order.push_back(t);
        for (int s : dag.successors(t)) {
            if (--indegree[s] == 0) ready.push(s);
        }
    }

    if (order.size() != n) {
        throw std::runtime_error("topological_order: dag contains a cycle");
    }
    return order;
}

NaiveResult naive_schedule(const Dag& dag) {
    const std::size_t n = dag.num_tasks();
    const std::size_t p = dag.num_processors();

    std::vector<int> order = topological_order(dag);

    std::vector<double> proc_free(p, 0.0);
    std::vector<ScheduleEntry> assigned(n);

    for (std::size_t i = 0; i < order.size(); ++i) {
        int t = order[i];
        int proc = static_cast<int>(i % p);  // round-robin, ignores cost/load entirely
        const Task& task = dag.task(t);

        double ready_time = 0.0;
        for (int pred : task.preds) {
            const ScheduleEntry& pe = assigned[pred];
            double comm = (pe.processor == proc) ? 0.0 : dag.comm_cost(pred, t);
            ready_time = std::max(ready_time, pe.finish + comm);
        }

        // Naive: always append after the last task already on this
        // processor -- never inserted into an earlier idle gap.
        double start = std::max(ready_time, proc_free[proc]);
        double finish = start + task.cost[proc];

        assigned[t] = ScheduleEntry{t, proc, start, finish};
        proc_free[proc] = finish;
    }

    double makespan = 0.0;
    for (const auto& e : assigned) makespan = std::max(makespan, e.finish);

    NaiveResult result;
    result.schedule = std::move(assigned);
    result.order = std::move(order);
    result.makespan = makespan;
    return result;
}

}  // namespace heftsim
