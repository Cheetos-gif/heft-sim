// src/profile/probe.cpp
#include "probe.hpp"

#include "../exec/affinity.hpp"
#include "pressure.hpp"

#include <algorithm>
#include <ctime>
#include <stdexcept>
#include <thread>

namespace heftsim::profile {

namespace {

double now_sec() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);  // never wall clock (CLAUDE.md)
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}  // namespace

ProbeResult measure_solo(KernelType kernel, int core, const KernelConfig& cfg, int reps, unsigned seed) {
    ProbeResult result;
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(reps));

    for (int i = 0; i < reps; ++i) {
        Workspace ws;
        init_workspace(kernel, ws, seed + static_cast<unsigned>(i), cfg);

        bool pinned = false;
        std::thread t([&] {
            pinned = pin_current_thread(core);
            double t0 = now_sec();
            run_kernel(kernel, ws, cfg);
            samples.push_back(now_sec() - t0);
        });
        t.join();
        result.pinned = result.pinned && pinned;
    }

    result.seconds = median(samples);
    return result;
}

ProbeResult measure_contended(KernelType reporter, int coreA, KernelType interferer, int coreB,
                               const KernelConfig& cfg, int reps, unsigned seed) {
    if (coreA == coreB) {
        throw std::invalid_argument("measure_contended: coreA and coreB must be distinct");
    }

    ProbeResult result;
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(reps));

    for (int i = 0; i < reps; ++i) {
        PressureGenerator pressure(interferer, coreB, cfg, seed + 1000u + static_cast<unsigned>(i));
        pressure.start();

        Workspace ws;
        init_workspace(reporter, ws, seed + static_cast<unsigned>(i), cfg);

        bool pinned = false;
        std::thread t([&] {
            pinned = pin_current_thread(coreA);
            double t0 = now_sec();
            run_kernel(reporter, ws, cfg);
            samples.push_back(now_sec() - t0);
        });
        t.join();

        pressure.stop();  // dtor would do this anyway; explicit for clarity
        result.pinned = result.pinned && pinned && pressure.pinned();
    }

    result.seconds = median(samples);
    return result;
}

}  // namespace heftsim::profile
