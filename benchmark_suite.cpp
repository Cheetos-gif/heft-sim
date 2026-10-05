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
// a small corpus of generated DAGs (src/dag/generators.hpp), each
// executed for real on pinned threads (src/exec/dispatcher.hpp). Raw
// (uncalibrated) heft is deliberately excluded -- see
// tests/fixtures/topcuoglu_calibrated.cpp's own precedent: a
// predicted-vs-measured comparison is only meaningful once costs are in
// real seconds (calibrated); heft_schedule() against a generator's
// synthetic Task::cost table would be the same kind of arbitrary-unit
// comparison topcuoglu_exec.cpp already documents as uncalibrated.
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
// balanced, sparse, and densely-connected -- enough shape variety to see
// whether cheft's correction helps consistently or only in some
// topologies, without this sweep taking many minutes (each DAG is
// executed for real, three times, once per scheduler).
std::vector<DagSpec> build_corpus(int num_processors) {
    std::vector<DagSpec> specs;

    GeneratorConfig narrow_deep;
    narrow_deep.num_layers = 6;
    narrow_deep.min_tasks_per_layer = 1;
    narrow_deep.max_tasks_per_layer = 2;
    narrow_deep.edge_probability = 0.7;
    narrow_deep.num_processors = num_processors;
    specs.push_back({"narrow_deep", narrow_deep, 101});

    GeneratorConfig wide_shallow;
    wide_shallow.num_layers = 2;
    wide_shallow.min_tasks_per_layer = 5;
    wide_shallow.max_tasks_per_layer = 6;
    wide_shallow.edge_probability = 0.5;
    wide_shallow.num_processors = num_processors;
    specs.push_back({"wide_shallow", wide_shallow, 102});

    GeneratorConfig balanced;
    balanced.num_layers = 4;
    balanced.min_tasks_per_layer = 2;
    balanced.max_tasks_per_layer = 3;
    balanced.edge_probability = 0.5;
    balanced.num_processors = num_processors;
    specs.push_back({"balanced", balanced, 103});

    GeneratorConfig sparse;
    sparse.num_layers = 4;
    sparse.min_tasks_per_layer = 2;
    sparse.max_tasks_per_layer = 3;
    sparse.edge_probability = 0.15;
    sparse.num_processors = num_processors;
    specs.push_back({"sparse", sparse, 104});

    GeneratorConfig dense;
    dense.num_layers = 4;
    dense.min_tasks_per_layer = 2;
    dense.max_tasks_per_layer = 3;
    dense.edge_probability = 0.9;
    dense.num_processors = num_processors;
    specs.push_back({"dense", dense, 105});

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
