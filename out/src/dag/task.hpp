// src/dag/task.hpp
//
// A single DAG node: an id, its per-processor execution cost, the ids of
// its predecessors, and a kernel-type tag used later (src/kernels/) to pick
// the real workload a task runs. The paper's reference example (Topcuoglu
// et al. 2002, Fig. 1) has no notion of kernel type -- that field is this
// project's addition and is not asserted against the paper.

#pragma once

#include <vector>

namespace heftsim {

enum class KernelType { COMPUTE_BOUND, MEMORY_BOUND };

struct Task {
    int id = -1;
    std::vector<double> cost;  // cost[p] = execution time of this task on processor p
    std::vector<int> preds;    // ids of predecessor tasks (edges point pred -> this)
    KernelType kernel = KernelType::COMPUTE_BOUND;
};

}  // namespace heftsim
