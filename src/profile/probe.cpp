// src/profile/probe.cpp
#include "probe.hpp"

#include "../exec/affinity.hpp"
#include "pressure.hpp"

#include <algorithm>
#include <ctime>
#include <memory>
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

ProbeResult measure_contended_multi(KernelType reporter, int reporter_core,
                                     const std::vector<std::pair<KernelType, int>>& interferers,
                                     const KernelConfig& cfg, int reps, unsigned seed) {
    for (const auto& [k, core] : interferers) {
        (void)k;
        if (core == reporter_core) {
            throw std::invalid_argument("measure_contended_multi: an interferer core equals reporter_core");
        }
    }
    for (std::size_t i = 0; i < interferers.size(); ++i) {
        for (std::size_t j = i + 1; j < interferers.size(); ++j) {
            if (interferers[i].second == interferers[j].second) {
                throw std::invalid_argument("measure_contended_multi: two interferers share a core");
            }
        }
    }

    ProbeResult result;
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(reps));

    for (int i = 0; i < reps; ++i) {
        std::vector<std::unique_ptr<PressureGenerator>> pressures;
        pressures.reserve(interferers.size());
        for (std::size_t k = 0; k < interferers.size(); ++k) {
            auto [kernel, core] = interferers[k];
            pressures.push_back(std::make_unique<PressureGenerator>(
                kernel, core, cfg, seed + 1000u + static_cast<unsigned>(i * 100 + k)));
            pressures.back()->start();
        }

        Workspace ws;
        init_workspace(reporter, ws, seed + static_cast<unsigned>(i), cfg);

        bool pinned = false;
        std::thread t([&] {
            pinned = pin_current_thread(reporter_core);
            double t0 = now_sec();
            run_kernel(reporter, ws, cfg);
            samples.push_back(now_sec() - t0);
        });
        t.join();

        bool interferers_pinned = true;
        for (auto& p : pressures) {
            p->stop();
            interferers_pinned = interferers_pinned && p->pinned();
        }
        result.pinned = result.pinned && pinned && interferers_pinned;
    }

    result.seconds = median(samples);
    return result;
}

}  // namespace heftsim::profile
