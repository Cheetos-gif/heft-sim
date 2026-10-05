// src/profile/probe.hpp
//
// Reporter/interferer measurement, after Mars et al. (Bubble-Up, ASPLOS
// 2011) -- the same methodology contention_pilot.cpp pioneered for this
// project, lifted into a reusable library so src/profile/calibration.cpp
// can sweep it over every (kernel, processor) and (kernel, kernel) pair
// instead of a fixed set of command-line-selected cores.
//
// measure_contended guarantees the reporter is contended for the *entire*
// measured window: the interferer (src/profile/pressure.hpp) starts before
// the reporter's timed pass and keeps looping until the reporter finishes.
// A plain "run both once and see who finishes first" design does not give
// this guarantee -- the shorter task finishes early and partly runs solo.

#pragma once

#include "../kernels/kernel.hpp"

#include <vector>

namespace heftsim::profile {

struct ProbeResult {
    double seconds = 0.0;  // median over reps
    bool pinned = true;    // AND of every thread's pin success across every rep
};

// Median wall-clock time (CLOCK_MONOTONIC) of one full run_kernel(kernel,
// ...) pass on `core`, over `reps` repetitions, with nothing else
// intentionally running on that core. `seed` seeds the first rep's
// Workspace; each subsequent rep increments it so repeated passes don't
// all chase an identical permutation (irrelevant to timing, but avoids any
// risk of a pathological single permutation skewing the median).
ProbeResult measure_solo(KernelType kernel, int core, const KernelConfig& cfg, int reps, unsigned seed = 1);

// Median wall-clock time of `reporter` on coreA while `interferer` loops
// continuously on coreB for the reporter's entire measured window.
// coreA and coreB must be distinct, or the reporter and interferer would
// be pinned to the same core and "contention" would really be preemption.
ProbeResult measure_contended(KernelType reporter, int coreA, KernelType interferer, int coreB,
                               const KernelConfig& cfg, int reps, unsigned seed = 1);

}  // namespace heftsim::profile
