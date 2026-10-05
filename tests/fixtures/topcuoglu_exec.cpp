// tests/fixtures/topcuoglu_exec.cpp
//
// End-to-end comparison runner: build the Topcuoglu 2002 Fig. 1 DAG, run
// it through every registered Scheduler (src/sched/scheduler.hpp), execute
// each resulting schedule for real on pinned threads
// (src/exec/dispatcher.hpp) running the COMPUTE/MEMORY kernels
// (src/kernels/kernel.hpp), and print a predicted-vs-measured table.
// Every run's event log is written to out/<scheduler>.csv.
//
// Predicted and measured are NOT expected to match -- the per-task cost
// table is paper-normalized time units, not seconds, and this project's
// whole point is measuring how far real contention (and, for naive, a
// deliberately bad placement policy) pushes measured away from predicted
// (CLAUDE.md).
//
// Build:  g++ -std=c++17 -O2 -pthread -Wall -Wextra -Isrc -o
//         tests/fixtures/topcuoglu_exec tests/fixtures/topcuoglu_exec.cpp
//         src/dag/dag.cpp src/sched/heft.cpp src/sched/naive.cpp
//         src/kernels/kernel.cpp src/exec/affinity.cpp
//         src/exec/event_log.cpp src/exec/dispatcher.cpp
// Run:    ./tests/fixtures/topcuoglu_exec

#include "../../src/exec/dispatcher.hpp"
#include "../../src/sched/heft.hpp"
#include "../../src/sched/naive.hpp"
#include "../../src/sched/scheduler.hpp"
#include "topcuoglu_dag.hpp"

#include <cstdio>
#include <memory>
#include <sys/stat.h>
#include <vector>

using namespace heftsim;

namespace {

struct Row {
    std::string name;
    double predicted = 0.0;
    double measured = 0.0;
    double error_pct = 0.0;
    double throughput = 0.0;  // tasks/sec, measured
};

}  // namespace

int main() {
    Dag dag = fixtures::build_topcuoglu_dag();

    std::vector<std::unique_ptr<Scheduler>> schedulers;
    schedulers.push_back(std::make_unique<HeftScheduler>());
    schedulers.push_back(std::make_unique<NaiveScheduler>());

    mkdir("out", 0755);  // ignore EEXIST -- fine if it's already there

    std::vector<Row> rows;
    for (const auto& sched : schedulers) {
        Schedule prediction = sched->schedule(dag);

        Dispatcher dispatcher(dag, prediction);
        ExecutionResult measured = dispatcher.run();

        if (!measured.all_pinned) {
            std::fprintf(stderr,
                          "[warn] %s: at least one worker thread failed to pin to its core -- "
                          "measured timings are unreliable\n",
                          sched->name().c_str());
        }

        std::string csv_path = "out/" + sched->name() + ".csv";
        write_event_log_csv(measured.log, csv_path);

        Row row;
        row.name = sched->name();
        row.predicted = prediction.makespan;
        row.measured = measured.measured_makespan;
        row.error_pct = (row.predicted > 0.0) ? (row.measured - row.predicted) / row.predicted * 100.0 : 0.0;
        row.throughput = (row.measured > 0.0) ? static_cast<double>(dag.num_tasks()) / row.measured : 0.0;
        rows.push_back(row);

        std::printf("%s: wrote %zu events to %s\n", sched->name().c_str(), measured.log.size(), csv_path.c_str());
    }

    std::printf("\n%-8s %16s %16s %10s %14s\n", "sched", "predicted", "measured(s)", "error%", "throughput/s");
    std::printf("%-8s %16s %16s %10s %14s\n", "-----", "---------", "-----------", "------", "------------");
    for (const auto& r : rows) {
        std::printf("%-8s %16.2f %16.4f %9.1f%% %14.2f\n", r.name.c_str(), r.predicted, r.measured, r.error_pct,
                     r.throughput);
    }
    std::printf(
        "\nNOTE: 'predicted' is in the paper's arbitrary cost-table units, not\n"
        "seconds like 'measured' -- error%% here is therefore NOT a calibrated\n"
        "prediction error, only a same-DAG comparison of the two schedulers'\n"
        "relative predicted-vs-measured behavior. For a real, unit-matched error\n"
        "(Task::cost calibrated from measured per-kernel timings instead of the\n"
        "paper's table), see tests/fixtures/topcuoglu_calibrated.cpp\n"
        "(src/profile/).\n");

    return 0;
}
