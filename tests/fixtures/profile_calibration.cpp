// tests/fixtures/profile_calibration.cpp
//
// Structural regression test for src/profile/. Unlike tests/fixtures/
// topcuoglu.cpp (which checks exact numbers against a published paper),
// real measured timings vary run to run and machine to machine -- there
// is no "expected" sigma or pi value to assert bit-for-bit. What *is*
// invariant, and what this file checks:
//   - every measured quantity is positive and finite (a kernel pass can
//     never measure as zero or negative time);
//   - CalibrationProfile::cost() correctly indexes sigma and rejects an
//     out-of-range processor, same fail-loudly discipline as
//     src/dag/dag.cpp;
//   - measure_contended rejects coreA == coreB (contention requires two
//     distinct cores -- see src/profile/probe.hpp);
//   - a kernel contended against *itself* on a different core is not
//     wildly faster than solo (a loose sanity bound, not a precise
//     physical claim -- real contention can occasionally measure slightly
//     below 1.0x on a noisy/shared CI host, but a correct measurement
//     should never show a reporter running several times faster merely
//     because another thread started hammering a different core);
//   - measure_handoff rejects producer_core == consumer_core, measures a
//     non-negative latency, and CalibrationProfile::comm_cost()/
//     mean_comm_cost() index and average it correctly (src/profile/
//     comm_probe.hpp).
//
// Uses a deliberately small KernelConfig (not the demo-sized default) so
// this regression test runs in seconds, not the multi-second calibration
// sweep tests/fixtures/topcuoglu_calibrated.cpp performs for its own
// (much larger) demo-quality measurement.
//
// Build:  g++ -std=c++17 -O2 -pthread -Wall -Wextra -Isrc -o
//         tests/fixtures/profile_calibration
//         tests/fixtures/profile_calibration.cpp
//         src/kernels/kernel.cpp src/exec/affinity.cpp
//         src/profile/pressure.cpp src/profile/probe.cpp
//         src/profile/comm_probe.cpp src/profile/calibration.cpp
//         src/profile/calibrate_sweep.cpp
// Run:    ./tests/fixtures/profile_calibration

#include "../../src/profile/calibration.hpp"
#include "../../src/profile/comm_probe.hpp"
#include "../../src/profile/probe.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>

using namespace heftsim;
using namespace heftsim::profile;

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

void check_close(double actual, double expected, double tol, const std::string& what) {
    ++g_checks;
    if (std::fabs(actual - expected) > tol) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s (expected %.6f, got %.6f)\n", what.c_str(), expected, actual);
    }
}

// Small/fast config: this test only needs genuine, finite, positive
// timings -- not demo-realistic kernel sizes.
KernelConfig small_config() {
    KernelConfig cfg;
    cfg.mem_buf_bytes = 1ull << 20;  // 1 MB
    cfg.mem_steps = 50'000;
    cfg.comp_doubles = 256;
    cfg.comp_iters = 5'000;
    cfg.heartbeat_chunks = 1;
    return cfg;
}

}  // namespace

int main() {
    KernelConfig cfg = small_config();

    // --- measure_solo: positive, finite ---
    ProbeResult solo_compute = measure_solo(KernelType::COMPUTE_BOUND, 0, cfg, 3, 1);
    ProbeResult solo_memory = measure_solo(KernelType::MEMORY_BOUND, 0, cfg, 3, 2);
    check(solo_compute.seconds > 0.0, "measure_solo(COMPUTE) > 0");
    check(solo_memory.seconds > 0.0, "measure_solo(MEMORY) > 0");

    // --- measure_contended: rejects coreA == coreB ---
    bool threw = false;
    try {
        measure_contended(KernelType::COMPUTE_BOUND, 0, KernelType::COMPUTE_BOUND, 0, cfg, 1, 1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "measure_contended(coreA == coreB) throws std::invalid_argument");

    // --- measure_contended: positive, finite, not absurdly faster than solo ---
    unsigned hc = std::thread::hardware_concurrency();
    if (hc >= 2) {
        ProbeResult contended = measure_contended(KernelType::MEMORY_BOUND, 0, KernelType::MEMORY_BOUND, 1, cfg, 3, 3);
        check(contended.seconds > 0.0, "measure_contended(MEMORY, MEMORY) > 0");
        // Loose sanity bound: a correct contended measurement should not
        // show the reporter running more than 3x *faster* than solo just
        // because an interferer started on another core.
        check(contended.seconds > solo_memory.seconds * 0.3,
              "measure_contended(MEMORY, MEMORY) is not implausibly faster than solo");
    } else {
        std::printf("(skipping measure_contended sanity check -- fewer than 2 cores available)\n");
    }

    // --- CalibrationProfile: sigma positive, pi positive, cost() correct ---
    CalibrationProfile profile = calibrate(/*num_processors=*/2, cfg, /*reps=*/3, /*seed=*/10);
    check(profile.num_processors == 2, "calibrate: num_processors echoed back");
    check(profile.sigma.size() == 2, "calibrate: sigma sized by num_processors");

    for (std::size_t p = 0; p < profile.num_processors; ++p) {
        check(profile.sigma[p][0] > 0.0, "sigma[" + std::to_string(p) + "][COMPUTE] > 0");
        check(profile.sigma[p][1] > 0.0, "sigma[" + std::to_string(p) + "][MEMORY] > 0");
    }
    for (std::size_t ri = 0; ri < kNumKernels; ++ri) {
        for (std::size_t ii = 0; ii < kNumKernels; ++ii) {
            check(profile.pi[ri][ii] > 0.0, "pi[" + std::to_string(ri) + "][" + std::to_string(ii) + "] > 0");
        }
    }

    check(profile.cost(KernelType::COMPUTE_BOUND, 0) == profile.sigma[0][0], "cost(COMPUTE, 0) == sigma[0][0]");
    check(profile.cost(KernelType::MEMORY_BOUND, 1) == profile.sigma[1][1], "cost(MEMORY, 1) == sigma[1][1]");

    bool cost_threw = false;
    try {
        profile.cost(KernelType::COMPUTE_BOUND, 99);
    } catch (const std::out_of_range&) {
        cost_threw = true;
    }
    check(cost_threw, "cost(_, out-of-range processor) throws std::out_of_range");

    // --- measure_handoff: rejects producer_core == consumer_core ---
    bool handoff_threw = false;
    try {
        measure_handoff(0, 0, 1, 1);
    } catch (const std::invalid_argument&) {
        handoff_threw = true;
    }
    check(handoff_threw, "measure_handoff(producer_core == consumer_core) throws std::invalid_argument");

    // --- measure_handoff: non-negative, finite latency ---
    if (hc >= 2) {
        CommProbeResult handoff = measure_handoff(0, 1, 3, 11);
        check(handoff.seconds >= 0.0, "measure_handoff(0, 1) >= 0");
    } else {
        std::printf("(skipping measure_handoff sanity check -- fewer than 2 cores available)\n");
    }

    // --- CalibrationProfile: comm sized/indexed/averaged correctly ---
    check(profile.comm.size() == 2, "calibrate: comm sized by num_processors");
    for (std::size_t p = 0; p < profile.num_processors; ++p) {
        check(profile.comm[p].size() == 2, "calibrate: comm[" + std::to_string(p) + "] sized by num_processors");
        check(profile.comm_cost(p, p) == 0.0, "comm_cost(" + std::to_string(p) + ", " + std::to_string(p) + ") == 0");
    }
    for (std::size_t from = 0; from < profile.num_processors; ++from) {
        for (std::size_t to = 0; to < profile.num_processors; ++to) {
            check(profile.comm_cost(from, to) >= 0.0,
                  "comm_cost(" + std::to_string(from) + ", " + std::to_string(to) + ") >= 0");
        }
    }
    check(profile.comm_cost(0, 1) == profile.comm[0][1], "comm_cost(0, 1) == comm[0][1]");

    bool comm_threw = false;
    try {
        profile.comm_cost(0, 99);
    } catch (const std::out_of_range&) {
        comm_threw = true;
    }
    check(comm_threw, "comm_cost(_, out-of-range processor) throws std::out_of_range");

    // mean_comm_cost() over 2 processors is just the single off-diagonal
    // pair's average, i.e. (comm[0][1] + comm[1][0]) / 2.
    check_close(profile.mean_comm_cost(), (profile.comm[0][1] + profile.comm[1][0]) / 2.0, 1e-9,
                "mean_comm_cost() == average of off-diagonal entries");

    CalibrationProfile single_proc;
    single_proc.num_processors = 1;
    single_proc.comm = {{0.0}};
    check(single_proc.mean_comm_cost() == 0.0, "mean_comm_cost() == 0 when num_processors < 2");

    // --- combo_index(): pure function, deterministic regardless of hardware ---
    check(combo_index(KernelType::COMPUTE_BOUND, KernelType::COMPUTE_BOUND) == 0,
          "combo_index(COMPUTE, COMPUTE) == 0");
    check(combo_index(KernelType::COMPUTE_BOUND, KernelType::MEMORY_BOUND) == 1,
          "combo_index(COMPUTE, MEMORY) == 1");
    check(combo_index(KernelType::MEMORY_BOUND, KernelType::COMPUTE_BOUND) == 1,
          "combo_index(MEMORY, COMPUTE) == 1 (order-independent)");
    check(combo_index(KernelType::MEMORY_BOUND, KernelType::MEMORY_BOUND) == 2,
          "combo_index(MEMORY, MEMORY) == 2");

    // --- CalibrationProfile: pi3 positive (identity fallback is also > 0) ---
    for (std::size_t ri = 0; ri < kNumKernels; ++ri) {
        for (std::size_t c = 0; c < kNumInterfererCombos; ++c) {
            check(profile.pi3[ri][c] > 0.0,
                  "pi3[" + std::to_string(ri) + "][" + std::to_string(c) + "] > 0");
        }
    }

    // --- measure_contended_multi: rejects a shared core (reporter/interferer) ---
    bool multi_threw_a = false;
    try {
        measure_contended_multi(KernelType::MEMORY_BOUND, 0, {{KernelType::MEMORY_BOUND, 0}}, cfg, 1, 1);
    } catch (const std::invalid_argument&) {
        multi_threw_a = true;
    }
    check(multi_threw_a, "measure_contended_multi(interferer shares reporter_core) throws std::invalid_argument");

    // --- measure_contended_multi: rejects two interferers sharing a core ---
    if (hc >= 3) {
        bool multi_threw_b = false;
        try {
            measure_contended_multi(KernelType::MEMORY_BOUND, 0,
                                     {{KernelType::MEMORY_BOUND, 1}, {KernelType::COMPUTE_BOUND, 1}}, cfg, 1, 1);
        } catch (const std::invalid_argument&) {
            multi_threw_b = true;
        }
        check(multi_threw_b, "measure_contended_multi(two interferers share a core) throws std::invalid_argument");

        // --- measure_contended_multi: positive, finite with genuinely distinct cores ---
        ProbeResult multi = measure_contended_multi(
            KernelType::MEMORY_BOUND, 0, {{KernelType::MEMORY_BOUND, 1}, {KernelType::MEMORY_BOUND, 2}}, cfg, 3, 21);
        check(multi.seconds > 0.0, "measure_contended_multi(2 interferers) > 0");
    } else {
        std::printf("(skipping measure_contended_multi 3-core checks -- fewer than 3 cores available)\n");
    }

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
