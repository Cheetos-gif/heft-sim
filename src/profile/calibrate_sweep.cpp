// src/profile/calibrate_sweep.cpp
//
// calibrate()'s implementation, split out from calibration.cpp (see that
// file's header comment) because this is the half that actually runs
// measurements and therefore needs src/profile/probe.hpp (and, through
// it, pressure.hpp, kernels/kernel.hpp, exec/affinity.hpp).
#include "calibration.hpp"

#include "../exec/affinity.hpp"
#include "comm_probe.hpp"
#include "probe.hpp"

#include <thread>

namespace heftsim::profile {

namespace {
constexpr KernelType kKernels[kNumKernels] = {KernelType::COMPUTE_BOUND, KernelType::MEMORY_BOUND};
}  // namespace

CalibrationProfile calibrate(std::size_t num_processors, const KernelConfig& cfg, int reps, unsigned seed) {
    CalibrationProfile profile;
    profile.num_processors = num_processors;
    profile.sigma.assign(num_processors, std::array<double, kNumKernels>{});
    profile.comm.assign(num_processors, std::vector<double>(num_processors, 0.0));
    profile.all_pinned = true;

    for (std::size_t p = 0; p < num_processors; ++p) {
        int core = core_for_processor(static_cast<int>(p));
        for (std::size_t ki = 0; ki < kNumKernels; ++ki) {
            ProbeResult r = measure_solo(kKernels[ki], core, cfg, reps,
                                          seed + static_cast<unsigned>((p * kNumKernels + ki) * 100));
            profile.sigma[p][ki] = r.seconds;
            profile.all_pinned = profile.all_pinned && r.pinned;
        }
    }

    unsigned hc = std::thread::hardware_concurrency();
    bool have_pair = hc >= 2;
    const int coreA = 0;
    const int coreB = 1;

    for (std::size_t ri = 0; ri < kNumKernels; ++ri) {
        for (std::size_t ii = 0; ii < kNumKernels; ++ii) {
            if (!have_pair) {
                profile.pi[ri][ii] = 1.0;
                continue;
            }
            ProbeResult solo = measure_solo(kKernels[ri], coreA, cfg, reps,
                                             seed + 5000u + static_cast<unsigned>((ri * kNumKernels + ii) * 100));
            ProbeResult contended = measure_contended(kKernels[ri], coreA, kKernels[ii], coreB, cfg, reps,
                                                        seed + 9000u + static_cast<unsigned>((ri * kNumKernels + ii) * 100));
            profile.all_pinned = profile.all_pinned && solo.pinned && contended.pinned;
            profile.pi[ri][ii] = (solo.seconds > 0.0) ? contended.seconds / solo.seconds : 1.0;
        }
    }

    bool have_triple = hc >= 3;
    const int coreC = 2;
    static constexpr std::array<std::array<KernelType, 2>, kNumInterfererCombos> kCombos = {{
        {KernelType::COMPUTE_BOUND, KernelType::COMPUTE_BOUND},
        {KernelType::COMPUTE_BOUND, KernelType::MEMORY_BOUND},
        {KernelType::MEMORY_BOUND, KernelType::MEMORY_BOUND},
    }};

    for (std::size_t ri = 0; ri < kNumKernels; ++ri) {
        for (std::size_t c = 0; c < kNumInterfererCombos; ++c) {
            if (!have_triple) {
                profile.pi3[ri][c] = 1.0;
                continue;
            }
            KernelType ka = kCombos[c][0];
            KernelType kb = kCombos[c][1];
            ProbeResult solo = measure_solo(kKernels[ri], coreA, cfg, reps,
                                             seed + 17000u + static_cast<unsigned>((ri * kNumInterfererCombos + c) * 100));
            ProbeResult contended = measure_contended_multi(
                kKernels[ri], coreA, {{ka, coreB}, {kb, coreC}}, cfg, reps,
                seed + 21000u + static_cast<unsigned>((ri * kNumInterfererCombos + c) * 100));
            profile.all_pinned = profile.all_pinned && solo.pinned && contended.pinned;
            profile.pi3[ri][c] = (solo.seconds > 0.0) ? contended.seconds / solo.seconds : 1.0;
        }
    }

    for (std::size_t from = 0; from < num_processors; ++from) {
        int from_core = core_for_processor(static_cast<int>(from));
        for (std::size_t to = 0; to < num_processors; ++to) {
            if (from == to) continue;  // comm[p][p] stays 0 -- no cross-core handoff within one processor
            int to_core = core_for_processor(static_cast<int>(to));
            if (from_core == to_core) continue;  // aliased onto the same physical core -- stays 0, see header comment
            CommProbeResult r = measure_handoff(from_core, to_core, reps,
                                                 seed + 13000u + static_cast<unsigned>((from * num_processors + to) * 100));
            profile.comm[from][to] = r.seconds;
            profile.all_pinned = profile.all_pinned && r.pinned;
        }
    }

    return profile;
}

}  // namespace heftsim::profile
