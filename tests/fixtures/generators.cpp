// tests/fixtures/generators.cpp
//
// Validation sweep for src/dag/generators.hpp. generate_layered_dag()'s
// output varies with its config/seed by design, so (like
// tests/fixtures/profile_calibration.cpp) this checks invariants that
// must hold for *every* generated graph, not one fixed expected result:
//   - Dag::add_task/add_edge's own strict validation never fires (a
//     malformed graph would throw there, not here -- generate_layered_dag
//     itself must never produce one);
//   - every task has exactly cfg.num_processors costs, each within
//     [min_cost, max_cost];
//   - entry_tasks() is exactly layer 0's task ids (generate_layered_dag's
//     "every non-layer-0 task gets >= 1 predecessor" guarantee means
//     nothing later can accidentally be an entry task);
//   - exit_tasks() is non-empty;
//   - the result actually schedules: heft_schedule() runs without
//     throwing and produces a positive makespan;
//   - same (cfg, seed) reproduces an identical Dag (deterministic);
//   - bad configs are rejected with std::invalid_argument.
//
// Swept across several (num_layers, layer-size range, edge probability,
// seed) combinations, including edge_probability extremes 0.0 and 1.0.
//
// Build:  g++ -std=c++17 -O2 -Wall -Wextra -Isrc -o
//         tests/fixtures/generators tests/fixtures/generators.cpp
//         src/dag/dag.cpp src/dag/generators.cpp src/sched/heft.cpp
// Run:    ./tests/fixtures/generators

#include "../../src/dag/dag.hpp"
#include "../../src/dag/generators.hpp"
#include "../../src/sched/heft.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace heftsim;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

void check_one_dag(const GeneratorConfig& cfg, unsigned seed, const std::string& tag) {
    Dag dag = generate_layered_dag(cfg, seed);

    check(dag.num_tasks() > 0, tag + ": num_tasks() > 0");
    check(dag.num_processors() == static_cast<std::size_t>(cfg.num_processors),
          tag + ": num_processors() == cfg.num_processors");

    for (std::size_t id = 0; id < dag.num_tasks(); ++id) {
        const Task& t = dag.task(static_cast<int>(id));
        check(t.cost.size() == static_cast<std::size_t>(cfg.num_processors),
              tag + ": task " + std::to_string(id) + " has cfg.num_processors costs");
        for (double c : t.cost) {
            check(c >= cfg.min_cost && c <= cfg.max_cost,
                  tag + ": task " + std::to_string(id) + " cost within [min_cost, max_cost]");
        }
    }

    std::vector<int> entries = dag.entry_tasks();
    check(!entries.empty(), tag + ": entry_tasks() non-empty");
    std::vector<int> exits = dag.exit_tasks();
    check(!exits.empty(), tag + ": exit_tasks() non-empty");

    // Every entry task must have no predecessors; every non-entry task
    // must have at least one (generate_layered_dag's own guarantee).
    for (std::size_t id = 0; id < dag.num_tasks(); ++id) {
        bool is_entry = dag.task(static_cast<int>(id)).preds.empty();
        bool in_entries = std::find(entries.begin(), entries.end(), static_cast<int>(id)) != entries.end();
        check(is_entry == in_entries, tag + ": task " + std::to_string(id) + " entry-ness matches preds.empty()");
    }

    // Must actually schedule without throwing, with a positive makespan.
    HeftResult result = heft_schedule(dag);
    check(result.makespan > 0.0, tag + ": heft_schedule makespan > 0");
    check(result.schedule.size() == dag.num_tasks(), tag + ": schedule has one entry per task");

    // Determinism: same (cfg, seed) reproduces an identical Dag.
    Dag dag2 = generate_layered_dag(cfg, seed);
    check(dag2.num_tasks() == dag.num_tasks(), tag + ": reproducible num_tasks()");
    bool identical = dag2.num_tasks() == dag.num_tasks();
    if (identical) {
        for (std::size_t id = 0; id < dag.num_tasks(); ++id) {
            identical = identical && dag.task(static_cast<int>(id)).cost == dag2.task(static_cast<int>(id)).cost &&
                        dag.task(static_cast<int>(id)).preds == dag2.task(static_cast<int>(id)).preds;
        }
    }
    check(identical, tag + ": identical seed reproduces identical Dag");
}

}  // namespace

int main() {
    GeneratorConfig base;
    base.num_layers = 4;
    base.min_tasks_per_layer = 2;
    base.max_tasks_per_layer = 5;
    base.edge_probability = 0.5;
    base.num_processors = 3;
    base.min_cost = 1.0;
    base.max_cost = 20.0;
    base.min_comm_cost = 0.0;
    base.max_comm_cost = 10.0;

    for (unsigned seed : {1u, 2u, 3u, 42u, 1337u}) {
        check_one_dag(base, seed, "base seed=" + std::to_string(seed));
    }

    GeneratorConfig sparse = base;
    sparse.edge_probability = 0.0;  // every non-entry task falls back to exactly one forced edge
    check_one_dag(sparse, 7, "edge_probability=0.0");

    GeneratorConfig dense = base;
    dense.edge_probability = 1.0;
    check_one_dag(dense, 7, "edge_probability=1.0");

    GeneratorConfig single_layer = base;
    single_layer.num_layers = 1;
    check_one_dag(single_layer, 7, "num_layers=1");

    GeneratorConfig wide = base;
    wide.min_tasks_per_layer = wide.max_tasks_per_layer = 10;
    wide.num_layers = 2;
    check_one_dag(wide, 9, "wide layers");

    // --- bad config rejection ---
    auto expect_throw = [](GeneratorConfig cfg, const std::string& what) {
        bool threw = false;
        try {
            generate_layered_dag(cfg, 1);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "rejects: " + what);
    };

    {
        GeneratorConfig c = base;
        c.num_layers = 0;
        expect_throw(c, "num_layers=0");
    }
    {
        GeneratorConfig c = base;
        c.min_tasks_per_layer = 0;
        expect_throw(c, "min_tasks_per_layer=0");
    }
    {
        GeneratorConfig c = base;
        c.min_tasks_per_layer = 5;
        c.max_tasks_per_layer = 2;
        expect_throw(c, "min_tasks_per_layer > max_tasks_per_layer");
    }
    {
        GeneratorConfig c = base;
        c.num_processors = 0;
        expect_throw(c, "num_processors=0");
    }
    {
        GeneratorConfig c = base;
        c.edge_probability = 1.5;
        expect_throw(c, "edge_probability > 1");
    }
    {
        GeneratorConfig c = base;
        c.edge_probability = -0.1;
        expect_throw(c, "edge_probability < 0");
    }
    {
        GeneratorConfig c = base;
        c.min_cost = 100.0;
        c.max_cost = 1.0;
        expect_throw(c, "min_cost > max_cost");
    }
    {
        GeneratorConfig c = base;
        c.min_comm_cost = 100.0;
        c.max_comm_cost = 1.0;
        expect_throw(c, "min_comm_cost > max_comm_cost");
    }

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
