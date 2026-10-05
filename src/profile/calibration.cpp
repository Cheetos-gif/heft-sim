// src/profile/calibration.cpp
//
// Deliberately kept separate from calibrate_sweep.cpp: CalibrationProfile
// is a plain data holder (src/sched/eheft.cpp and anything that hand-
// builds or deserializes a profile needs only this), while the actual
// measurement sweep (calibrate()) pulls in the whole probe/pressure/
// affinity chain. A consumer that already has a CalibrationProfile --
// hand-built in a test, or loaded from a prior run -- should not need to
// link pthread-based measurement machinery just to call cost().
#include "calibration.hpp"

#include <stdexcept>

namespace heftsim::profile {

double CalibrationProfile::cost(KernelType kernel, std::size_t processor) const {
    if (processor >= num_processors) {
        throw std::out_of_range("CalibrationProfile::cost: processor out of range");
    }
    return sigma[processor][kernel_index(kernel)];
}

double CalibrationProfile::comm_cost(std::size_t from, std::size_t to) const {
    if (from >= num_processors || to >= num_processors) {
        throw std::out_of_range("CalibrationProfile::comm_cost: processor out of range");
    }
    return comm[from][to];
}

double CalibrationProfile::mean_comm_cost() const {
    if (num_processors < 2) return 0.0;
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t from = 0; from < num_processors; ++from) {
        for (std::size_t to = 0; to < num_processors; ++to) {
            if (from == to) continue;
            sum += comm[from][to];
            ++count;
        }
    }
    return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

}  // namespace heftsim::profile
