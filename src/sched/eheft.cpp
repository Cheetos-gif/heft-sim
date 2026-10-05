// src/sched/eheft.cpp
#include "eheft.hpp"

#include <stdexcept>

namespace heftsim {

Dag calibrate_costs(const Dag& topology, const profile::CalibrationProfile& profile) {
    if (topology.num_processors() != profile.num_processors) {
        throw std::invalid_argument(
            "calibrate_costs: topology.num_processors() != profile.num_processors");
    }

    Dag out;
    const std::size_t n = topology.num_tasks();
    const std::size_t p = topology.num_processors();

    for (std::size_t id = 0; id < n; ++id) {
        const Task& src = topology.task(static_cast<int>(id));
        Task t;
        t.id = static_cast<int>(id);
        t.preds = src.preds;
        t.kernel = src.kernel;
        t.cost.resize(p);
        for (std::size_t proc = 0; proc < p; ++proc) {
            t.cost[proc] = profile.cost(t.kernel, proc);
        }
        out.add_task(std::move(t));
    }

    const double comm = profile.mean_comm_cost();
    for (std::size_t from = 0; from < n; ++from) {
        for (int to : topology.successors(static_cast<int>(from))) {
            // See eheft.hpp: a single mean cross-processor handoff cost,
            // not a processor-pair-specific one -- heft_schedule()'s own
            // cost model (src/sched/heft.cpp) takes one flat comm_cost
            // per edge.
            out.add_edge(static_cast<int>(from), to, comm);
        }
    }

    return out;
}

HeftResult eheft_schedule(const Dag& topology, const profile::CalibrationProfile& profile) {
    Dag calibrated = calibrate_costs(topology, profile);
    return heft_schedule(calibrated);
}

}  // namespace heftsim
