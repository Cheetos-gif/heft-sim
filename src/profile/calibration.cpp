// src/profile/calibration.cpp
#include "calibration.hpp"

#include "../exec/affinity.hpp"
#include "probe.hpp"

#include <stdexcept>
#include <thread>

namespace heftsim::profile {

double CalibrationProfile::cost(KernelType kernel, std::size_t processor) const {
    if (processor >= num_processors) {
        throw std::out_of_range("CalibrationProfile::cost: processor out of range");
    }
    return sigma[processor][kernel_index(kernel)];
}

namespace {
constexpr KernelType kKernels[kNumKernels] = {KernelType::COMPUTE_BOUND, KernelType::MEMORY_BOUND};
}  // namespace

CalibrationProfile calibrate(std::size_t num_processors, const KernelConfig& cfg, int reps, unsigned seed) {
    CalibrationProfile profile;
    profile.num_processors = num_processors;
    profile.sigma.assign(num_processors, std::array<double, kNumKernels>{});
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

    return profile;
}

}  // namespace heftsim::profile
