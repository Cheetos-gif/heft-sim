// src/profile/pressure.cpp
#include "pressure.hpp"

#include "../exec/affinity.hpp"

namespace heftsim::profile {

PressureGenerator::PressureGenerator(KernelType kernel, int core, KernelConfig cfg, unsigned seed)
    : kernel_(kernel), core_(core), cfg_(std::move(cfg)), seed_(seed) {}

PressureGenerator::~PressureGenerator() { stop(); }

void PressureGenerator::start() {
    init_workspace(kernel_, ws_, seed_, cfg_);
    started_ = true;
    thread_ = std::thread(&PressureGenerator::loop, this);
}

void PressureGenerator::stop() {
    if (!started_) return;
    stop_.store(true, std::memory_order_relaxed);
    if (thread_.joinable()) thread_.join();
    started_ = false;
}

void PressureGenerator::loop() {
    pinned_.store(pin_current_thread(core_), std::memory_order_relaxed);
    while (!stop_.load(std::memory_order_relaxed)) {
        run_kernel(kernel_, ws_, cfg_);
    }
}

}  // namespace heftsim::profile
