// src/dag/generators.hpp
//
// Synthetic DAG generation. tests/fixtures/topcuoglu_dag.hpp gives this
// project exactly one topology (10 tasks, fixed shape) -- not enough to
// say anything statistically about scheduler behavior across workload
// shapes. generate_layered_dag() produces varied, reproducible synthetic
// DAGs for that purpose (feeding, eventually, a multi-DAG benchmark
// harness and src/analysis/'s plots).

#pragma once

#include "dag.hpp"

namespace heftsim {

struct GeneratorConfig {
    int num_layers = 4;
    int min_tasks_per_layer = 1;
    int max_tasks_per_layer = 4;
    // Independent Bernoulli probability of an edge between each
    // (task in layer i, task in layer i+1) pair. Edges only ever go
    // between adjacent layers -- see generate_layered_dag()'s comment on
    // why that's what makes acyclicity free.
    double edge_probability = 0.5;
    int num_processors = 3;
    // Task::cost[p] is drawn uniformly from [min_cost, max_cost]
    // independently per processor, so generated tasks are heterogeneous
    // by construction (like the paper's own Table 1).
    double min_cost = 1.0;
    double max_cost = 20.0;
    double min_comm_cost = 0.0;
    double max_comm_cost = 20.0;
};

// Generates a random layered DAG:
//   - tasks are grouped into cfg.num_layers layers; each layer's size is
//     drawn uniformly from [min_tasks_per_layer, max_tasks_per_layer];
//   - every potential edge from a layer-i task to a layer-(i+1) task is
//     included independently with probability edge_probability. Edges
//     never go within a layer or skip a layer -- this layered
//     restriction is what guarantees the result is acyclic by
//     construction, with no separate cycle check needed (standard
//     practice for synthetic DAG scheduling benchmarks, e.g. the
//     generator used alongside the Topcuoglu/Sih-and-Lee family of HEFT
//     papers);
//   - any layer-(i+1) task that ends up with zero sampled predecessors is
//     given exactly one (a uniformly-chosen task in layer i), so only
//     layer 0's tasks are ever DAG entry tasks -- a generated graph never
//     has accidental extra entry points partway through;
//   - Task::cost[p] is drawn uniformly from [min_cost, max_cost] per
//     processor; Task::kernel alternates COMPUTE_BOUND/MEMORY_BOUND by
//     task id (same convention as tests/fixtures/topcuoglu_dag.hpp), so
//     the result's topology can also be run through
//     src/sched/eheft.hpp::calibrate_costs() for a calibrated comparison,
//     discarding these synthetic costs in favor of measured sigma;
//   - edge comm_cost is drawn uniformly from [min_comm_cost,
//     max_comm_cost].
//
// Deterministic: the same (cfg, seed) always produces the same Dag --
// generation uses only a single seeded std::mt19937_64, never global or
// time-based randomness.
//
// Throws std::invalid_argument if cfg.num_layers < 1,
// cfg.min_tasks_per_layer < 1, min_tasks_per_layer > max_tasks_per_layer,
// cfg.num_processors < 1, edge_probability outside [0,1], or either
// min/max cost pair has min > max -- same fail-loudly-on-malformed-input
// discipline as src/dag/dag.cpp.
Dag generate_layered_dag(const GeneratorConfig& cfg, unsigned seed);

}  // namespace heftsim
