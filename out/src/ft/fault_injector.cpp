// src/ft/fault_injector.cpp
#include "fault_injector.hpp"

#include <stdexcept>

namespace heftsim {

FaultInjector::FaultInjector(std::size_t num_tasks, unsigned seed) {
    if (num_tasks == 0) {
        throw std::invalid_argument("FaultInjector: num_tasks must be > 0");
    }
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, static_cast<int>(num_tasks) - 1);
    victim_ = dist(rng);
}

bool FaultInjector::should_kill(int task_id, int attempt) {
    if (attempt != 0 || task_id != victim_) return false;
    return !fired_.exchange(true, std::memory_order_acq_rel);
}

}  // namespace heftsim
