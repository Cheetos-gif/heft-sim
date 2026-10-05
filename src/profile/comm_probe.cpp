// src/profile/comm_probe.cpp
#include "comm_probe.hpp"

#include "../exec/affinity.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <stdexcept>
#include <thread>
#include <vector>

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

// One cache line's worth of "task result" payload -- see comm_probe.hpp's
// header comment on why this models a scalar handoff, not bulk transfer.
struct HandoffState {
    alignas(64) volatile std::uint8_t payload[64] = {};
    std::atomic<bool> go{false};
    std::atomic<int> seq{0};
    double t0 = 0.0;
};

}  // namespace

CommProbeResult measure_handoff(int producer_core, int consumer_core, int reps, unsigned seed) {
    if (producer_core == consumer_core) {
        throw std::invalid_argument("measure_handoff: producer_core and consumer_core must be distinct");
    }

    CommProbeResult result;
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(reps));

    for (int i = 0; i < reps; ++i) {
        HandoffState state;
        bool producer_pinned = false;
        bool consumer_pinned = false;
        double elapsed = 0.0;

        std::thread producer([&] {
            producer_pinned = pin_current_thread(producer_core);
            while (!state.go.load(std::memory_order_acquire)) {
                // spin -- see comm_probe.hpp on why this isn't condition_variable
            }
            state.t0 = now_sec();
            for (auto& b : state.payload) b = static_cast<std::uint8_t>(seed + static_cast<unsigned>(i));
            state.seq.store(1, std::memory_order_release);
        });

        std::thread consumer([&] {
            consumer_pinned = pin_current_thread(consumer_core);
            state.go.store(true, std::memory_order_release);
            while (state.seq.load(std::memory_order_acquire) == 0) {
                // spin
            }
            double t1 = now_sec();
            elapsed = t1 - state.t0;
        });

        producer.join();
        consumer.join();

        samples.push_back(elapsed);
        result.pinned = result.pinned && producer_pinned && consumer_pinned;
    }

    result.seconds = median(samples);
    return result;
}

}  // namespace heftsim::profile
