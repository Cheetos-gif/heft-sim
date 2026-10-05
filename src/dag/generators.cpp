// src/dag/generators.cpp
#include "generators.hpp"

#include <random>
#include <stdexcept>
#include <vector>

namespace heftsim {

Dag generate_layered_dag(const GeneratorConfig& cfg, unsigned seed) {
    if (cfg.num_layers < 1) {
        throw std::invalid_argument("generate_layered_dag: num_layers must be >= 1");
    }
    if (cfg.min_tasks_per_layer < 1 || cfg.min_tasks_per_layer > cfg.max_tasks_per_layer) {
        throw std::invalid_argument(
            "generate_layered_dag: require 1 <= min_tasks_per_layer <= max_tasks_per_layer");
    }
    if (cfg.num_processors < 1) {
        throw std::invalid_argument("generate_layered_dag: num_processors must be >= 1");
    }
    if (cfg.edge_probability < 0.0 || cfg.edge_probability > 1.0) {
        throw std::invalid_argument("generate_layered_dag: edge_probability must be in [0, 1]");
    }
    if (cfg.min_cost > cfg.max_cost) {
        throw std::invalid_argument("generate_layered_dag: min_cost must be <= max_cost");
    }
    if (cfg.min_comm_cost > cfg.max_comm_cost) {
        throw std::invalid_argument("generate_layered_dag: min_comm_cost must be <= max_comm_cost");
    }

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> layer_size_dist(cfg.min_tasks_per_layer, cfg.max_tasks_per_layer);
    std::uniform_real_distribution<double> unit_dist(0.0, 1.0);
    std::uniform_real_distribution<double> cost_dist(cfg.min_cost, cfg.max_cost);
    std::uniform_real_distribution<double> comm_dist(cfg.min_comm_cost, cfg.max_comm_cost);

    // Assign layer sizes and ids up front so predecessor ids (always in
    // an earlier layer) are known before any Task is constructed.
    std::vector<std::vector<int>> layer_ids(cfg.num_layers);
    int next_id = 0;
    for (int layer = 0; layer < cfg.num_layers; ++layer) {
        int size = layer_size_dist(rng);
        for (int i = 0; i < size; ++i) layer_ids[layer].push_back(next_id++);
    }

    // Sample predecessor sets (within [layer-1]) before constructing any
    // Task, since Task::preds must be final at add_task() time.
    std::vector<std::vector<int>> preds(next_id);
    for (int layer = 1; layer < cfg.num_layers; ++layer) {
        const auto& prev = layer_ids[layer - 1];
        std::uniform_int_distribution<std::size_t> prev_pick(0, prev.size() - 1);
        for (int id : layer_ids[layer]) {
            for (int p : prev) {
                if (unit_dist(rng) < cfg.edge_probability) preds[id].push_back(p);
            }
            if (preds[id].empty()) {
                // Guarantee every non-layer-0 task has >= 1 predecessor,
                // so only layer 0 ever contains entry tasks.
                preds[id].push_back(prev[prev_pick(rng)]);
            }
        }
    }

    Dag dag;
    for (int id = 0; id < next_id; ++id) {
        Task t;
        t.id = id;
        t.preds = preds[id];
        t.kernel = (id % 2 == 0) ? KernelType::COMPUTE_BOUND : KernelType::MEMORY_BOUND;
        t.cost.resize(static_cast<std::size_t>(cfg.num_processors));
        for (double& c : t.cost) c = cost_dist(rng);
        dag.add_task(std::move(t));
    }

    for (int id = 0; id < next_id; ++id) {
        for (int pred : preds[id]) {
            dag.add_edge(pred, id, comm_dist(rng));
        }
    }

    return dag;
}

}  // namespace heftsim
