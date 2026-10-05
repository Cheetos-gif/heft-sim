// tests/fixtures/topcuoglu_calibrated.cpp
//
// The unit-matched counterpart to topcuoglu_exec.cpp: calibrates sigma
// (src/profile/calibration.hpp) from real measured kernel passes on this
// machine, then runs eheft (src/sched/eheft.hpp -- HEFT with Task::cost
// substituted from sigma instead of the paper's arbitrary table) and
// naive (against the same calibrated costs) on the paper's Fig. 1
// topology, executes each resulting schedule for real on pinned threads,
// and prints predicted-vs-measured error -- this time both numbers are
// genuinely in seconds, so error% is a real prediction error, not just a
// same-DAG relative comparison (contrast with topcuoglu_exec.cpp's
// printed caveat).
//
// Also prints the extracted pi (contended/solo) table: diagnostic only
// for now -- src/sched/cheft.hpp (the scheduler meant to consume it)
// doesn't exist yet.
//
// Build:  g++ -std=c++17 -O2 -pthread -Wall -Wextra -Isrc -o
//         tests/fixtures/topcuoglu_calibrated
//         tests/fixtures/topcuoglu_calibrated.cpp
//         src/dag/dag.cpp src/sched/heft.cpp src/sched/naive.cpp
//         src/sched/eheft.cpp src/kernels/kernel.cpp src/exec/affinity.cpp
//         src/exec/event_log.cpp src/exec/dispatcher.cpp
//         src/profile/pressure.cpp src/profile/probe.cpp
//         src/profile/calibration.cpp src/profile/calibrate_sweep.cpp
// Run:    ./tests/fixtures/topcuoglu_calibrated

#include "../../src/exec/dispatcher.hpp"
#include "../../src/profile/calibration.hpp"
#include "../../src/sched/eheft.hpp"
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
    double throughput = 0.0;
};

const char* kernel_name(std::size_t idx) { return idx == 0 ? "COMPUTE" : "MEMORY "; }

}  // namespace

int main() {
    // Demo-sized kernel config -- same shape topcuoglu_exec.cpp's default
    // Dispatcher uses (KernelConfig{}), passed explicitly here so the
    // calibration sweep and the real execution measure/run the exact same
    // per-kernel workload. Mismatched configs would make sigma a
    // calibration for a different (and wrong) workload.
    KernelConfig cfg;

    std::printf("Calibrating sigma/pi from real measured kernel passes (this takes a few seconds)...\n");
    profile::CalibrationProfile profile = profile::calibrate(/*num_processors=*/3, cfg, /*reps=*/5);
    if (!profile.all_pinned) {
        std::fprintf(stderr, "[warn] calibration: at least one probe thread failed to pin -- sigma/pi are unreliable\n");
    }

    std::printf("\nsigma (solo seconds):\n%-10s %12s %12s\n", "processor", "COMPUTE", "MEMORY");
    for (std::size_t p = 0; p < profile.num_processors; ++p) {
        std::printf("%-10zu %12.6f %12.6f\n", p, profile.sigma[p][0], profile.sigma[p][1]);
    }

    std::printf("\npi (contended/solo, cores 0 vs 1):\n%-16s %12s %12s\n", "reporter\\interf", "COMPUTE", "MEMORY");
    for (std::size_t ri = 0; ri < profile::kNumKernels; ++ri) {
        std::printf("%-16s %12.3f %12.3f\n", kernel_name(ri), profile.pi[ri][0], profile.pi[ri][1]);
    }

    std::printf("\nmean cross-processor comm handoff: %.6f s (src/profile/comm_probe.hpp)\n",
                 profile.mean_comm_cost());

    Dag topology = fixtures::build_topcuoglu_dag();
    Dag calibrated = calibrate_costs(topology, profile);

    std::vector<std::unique_ptr<Scheduler>> schedulers;
    schedulers.push_back(std::make_unique<EheftScheduler>(profile));
    schedulers.push_back(std::make_unique<NaiveScheduler>());

    mkdir("out", 0755);  // ignore EEXIST -- fine if it's already there

    std::vector<Row> rows;
    for (const auto& sched : schedulers) {
        Schedule prediction = sched->schedule(calibrated);

        Dispatcher dispatcher(calibrated, prediction, cfg);
        ExecutionResult measured = dispatcher.run();

        if (!measured.all_pinned) {
            std::fprintf(stderr,
                          "[warn] %s: at least one worker thread failed to pin to its core -- "
                          "measured timings are unreliable\n",
                          sched->name().c_str());
        }

        std::string csv_path = "out/" + sched->name() + "_calibrated.csv";
        write_event_log_csv(measured.log, csv_path);

        Row row;
        row.name = sched->name();
        row.predicted = prediction.makespan;
        row.measured = measured.measured_makespan;
        row.error_pct = (row.predicted > 0.0) ? (row.measured - row.predicted) / row.predicted * 100.0 : 0.0;
        row.throughput = (row.measured > 0.0) ? static_cast<double>(calibrated.num_tasks()) / row.measured : 0.0;
        rows.push_back(row);

        std::printf("%s: wrote %zu events to %s\n", sched->name().c_str(), measured.log.size(), csv_path.c_str());
    }

    std::printf("\n%-8s %16s %16s %10s %14s\n", "sched", "predicted(s)", "measured(s)", "error%", "throughput/s");
    std::printf("%-8s %16s %16s %10s %14s\n", "-----", "------------", "-----------", "------", "------------");
    for (const auto& r : rows) {
        std::printf("%-8s %16.6f %16.6f %9.1f%% %14.2f\n", r.name.c_str(), r.predicted, r.measured, r.error_pct,
                     r.throughput);
    }
    std::printf(
        "\nNOTE: both columns are seconds, calibrated from this machine's real\n"
        "measured solo kernel passes (sigma) and cross-processor handoff\n"
        "latency (comm, printed above as a single mean -- see\n"
        "src/sched/eheft.hpp's calibrate_costs() comment on why one scalar,\n"
        "not a per-pair matrix) -- error%% above is a genuine predicted-vs-\n"
        "measured error, unlike topcuoglu_exec.cpp's paper-table comparison.\n"
        "sigma is a *solo* cost; it does not account for the contention the\n"
        "pi table above quantifies, which is exactly the error this project\n"
        "measures -- see CLAUDE.md.\n");

    return 0;
}
