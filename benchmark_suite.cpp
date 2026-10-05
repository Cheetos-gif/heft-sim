// benchmark_suite.cpp
//
// Multi-DAG, multi-scheduler experiment runner -- produces
// out/results.csv (src/exec/results_log.hpp), the machine-readable data
// analysis/ (Python) turns into the paper's predicted-vs-measured error
// tables/plots. Not a regression test: there is no fixed expected number
// to assert here, real measured contention varies by machine and DAG
// shape, which is exactly what this project exists to characterize. It
// lives at the repo root next to contention_pilot.cpp, not under
// tests/fixtures/, for the same reason that one does: it's an
// experiment, not a pass/fail check.
//
// Runs {naive, eheft, cheft} (src/sched/{naive,eheft,cheft}.hpp) against
// a corpus of generated DAGs (src/dag/generators.hpp, 5 shapes x 3 seed
// replicates each = 15 DAGs), each executed for real on pinned threads
// (src/exec/dispatcher.hpp). Raw (uncalibrated) heft is deliberately
// excluded -- see tests/fixtures/topcuoglu_calibrated.cpp's own
// precedent: a predicted-vs-measured comparison is only meaningful once
// costs are in real seconds (calibrated); heft_schedule() against a
// generator's synthetic Task::cost table would be the same kind of
// arbitrary-unit comparison topcuoglu_exec.cpp already documents as
// uncalibrated.
//
// Build: g++ -std=c++17 -O2 -pthread -Wall -Wextra -Isrc -o
//        benchmark_suite benchmark_suite.cpp
//        src/dag/dag.cpp src/dag/generators.cpp
//        src/sched/heft.cpp src/sched/naive.cpp src/sched/eheft.cpp
//        src/sched/cheft.cpp src/kernels/kernel.cpp src/exec/affinity.cpp
//        src/exec/event_log.cpp src/exec/results_log.cpp
//        src/exec/heartbeat.cpp src/exec/dispatcher.cpp
//        src/ft/fault_injector.cpp src/ft/heartbeat_monitor.cpp
//        src/profile/pressure.cpp src/profile/probe.cpp
//        src/profile/comm_probe.cpp src/profile/calibration.cpp
//        src/profile/calibrate_sweep.cpp
// Run:   ./benchmark_suite

#include "src/dag/generators.hpp"
#include "src/exec/dispatcher.hpp"
#include "src/exec/results_log.hpp"
#include "src/profile/calibration.hpp"
#include "src/sched/cheft.hpp"
#include "src/sched/eheft.hpp"
#include "src/sched/naive.hpp"
#include "src/sched/scheduler.hpp"

#include <cstdio>
#include <memory>
#include <sys/stat.h>
#include <vector>

using namespace heftsim;

namespace {

struct DagSpec {
    std::string id;
    GeneratorConfig cfg;
    unsigned seed;
};

// A small, deliberately varied corpus: narrow/deep, wide/shallow,
// balanced, sparse, and densely-connected shapes, each replicated across
// 3 seeds -- enough shape variety *and* enough repeats per shape to see
// whether cheft's correction helps consistently within a shape (not just
// on one lucky/unlucky random draw) or only sometimes, without this
// sweep taking many minutes (each of the resulting 15 DAGs is executed
// for real, three times, once per scheduler -- 45 real dispatcher runs
// total).
std::vector<DagSpec> build_corpus(int num_processors) {
    struct Shape {
        std::string name;
        int num_layers;
        int min_tasks;
        int max_tasks;
        double edge_probability;
        unsigned base_seed;
    };

    std::vector<Shape> shapes = {
        {"narrow_deep", 6, 1, 2, 0.7, 101},
        {"wide_shallow", 2, 5, 6, 0.5, 102},
        {"balanced", 4, 2, 3, 0.5, 103},
        {"sparse", 4, 2, 3, 0.15, 104},
        {"dense", 4, 2, 3, 0.9, 105},
    };

    constexpr int kReplicatesPerShape = 3;

    std::vector<DagSpec> specs;
    for (const auto& shape : shapes) {
        GeneratorConfig cfg;
        cfg.num_layers = shape.num_layers;
        cfg.min_tasks_per_layer = shape.min_tasks;
        cfg.max_tasks_per_layer = shape.max_tasks;
        cfg.edge_probability = shape.edge_probability;
        cfg.num_processors = num_processors;

        for (int rep = 0; rep < kReplicatesPerShape; ++rep) {
            unsigned seed = shape.base_seed + static_cast<unsigned>(rep) * 1000u;
            std::string id = shape.name + "_s" + std::to_string(rep + 1);
            specs.push_back({id, cfg, seed});
        }
    }

    return specs;
}

}  // namespace

int main() {
    constexpr std::size_t kNumProcessors = 3;
    KernelConfig cfg;  // demo-sized default, same as topcuoglu_calibrated.cpp

    std::printf("Calibrating sigma/pi/comm from real measured kernel passes...\n");
    profile::CalibrationProfile profile = profile::calibrate(kNumProcessors, cfg, /*reps=*/5);
    if (!profile.all_pinned) {
        std::fprintf(stderr, "[warn] calibration: at least one probe thread failed to pin -- numbers are unreliable\n");
    }

    mkdir("out", 0755);  // ignore EEXIST
    const std::string results_path = "out/results.csv";

    auto corpus = build_corpus(static_cast<int>(kNumProcessors));

    int rows_written = 0;
    for (const auto& spec : corpus) {
        Dag topology = generate_layered_dag(spec.cfg, spec.seed);
        Dag calibrated = calibrate_costs(topology, profile);

        std::vector<std::unique_ptr<Scheduler>> schedulers;
        schedulers.push_back(std::make_unique<NaiveScheduler>());
        schedulers.push_back(std::make_unique<EheftScheduler>(profile));
        schedulers.push_back(std::make_unique<CheftScheduler>(profile));

        std::printf("\n%s (%zu tasks):\n", spec.id.c_str(), calibrated.num_tasks());
        std::printf("  %-8s %14s %14s %9s\n", "sched", "predicted(s)", "measured(s)", "error%");

        for (const auto& sched : schedulers) {
            Schedule prediction = sched->schedule(calibrated);
            Dispatcher dispatcher(calibrated, prediction, cfg);
            ExecutionResult measured = dispatcher.run();

            if (!measured.all_pinned) {
                std::fprintf(stderr, "[warn] %s/%s: at least one worker thread failed to pin\n", spec.id.c_str(),
                              sched->name().c_str());
            }

            ResultRow row;
            row.dag_id = spec.id;
            row.num_tasks = static_cast<int>(calibrated.num_tasks());
            row.num_processors = static_cast<int>(calibrated.num_processors());
            row.scheduler = sched->name();
            row.predicted_sec = prediction.makespan;
            row.measured_sec = measured.measured_makespan;
            row.error_pct =
                (row.predicted_sec > 0.0) ? (row.measured_sec - row.predicted_sec) / row.predicted_sec * 100.0 : 0.0;
            row.throughput = (row.measured_sec > 0.0) ? static_cast<double>(row.num_tasks) / row.measured_sec : 0.0;

            append_result_row(results_path, row);
            ++rows_written;

            std::printf("  %-8s %14.4f %14.4f %8.1f%%\n", row.scheduler.c_str(), row.predicted_sec,
                        row.measured_sec, row.error_pct);
        }
    }

    std::printf("\nwrote %d rows to %s\n", rows_written, results_path.c_str());
    return 0;
}
