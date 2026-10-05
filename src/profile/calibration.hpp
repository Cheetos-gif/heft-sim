// src/profile/calibration.hpp
//
// sigma/pi/comm extraction: turns src/profile/probe.hpp and
// src/profile/comm_probe.hpp measurements into the calibration data this
// project's whole claim rests on (CLAUDE.md: "HEFT's cost model
// w(task, processor) ignores co-runner interference; we measure that
// error and correct for it").
//
//   sigma(kernel, processor) -- solo execution time, seconds. This is a
//   real, unit-matched replacement for the paper fixture's arbitrary
//   per-task cost table (see tests/fixtures/topcuoglu_exec.cpp's printed
//   caveat); src/sched/{heft,naive}.cpp consume whatever Task::cost says
//   without caring where it came from, so a Dag built from sigma values
//   makes 'predicted' and 'measured' both seconds.
//
//   pi(reporter_kernel, interferer_kernel) -- contended/solo ratio,
//   measured on one representative pair of distinct cores. Not consumed by
//   any scheduler yet: src/sched/ has naive, heft, and eheft only (all
//   contention-oblivious by design -- that obliviousness is exactly what
//   this project measures the error of). pi is extracted now so a future
//   contention-aware scheduler (CLAUDE.md's layout comment: "naive, heft,
//   eheft, cheft") has a ready-made calibration source instead of needing
//   its own profiling pass.
//
//   comm(from_processor, to_processor) -- median measured cross-core
//   handoff latency (src/profile/comm_probe.hpp), seconds. Consumed by
//   src/sched/eheft.hpp::calibrate_costs() via mean_comm_cost(): HEFT's
//   own cost model (src/sched/heft.cpp) takes one flat comm_cost per
//   edge, not a processor-pair matrix, so the full matrix here is
//   averaged down to a single representative cross-processor handoff
//   cost for that purpose. The full matrix is kept for diagnostics and
//   any future scheduler that can use per-pair costs directly.
//
// This header only measures; like every Scheduler, it never makes
// placement decisions and never touches a Dispatcher.

#pragma once

#include "../kernels/kernel.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace heftsim::profile {

// Index convenience: KernelType has exactly two values.
constexpr std::size_t kNumKernels = 2;
inline std::size_t kernel_index(KernelType k) { return k == KernelType::COMPUTE_BOUND ? 0 : 1; }

struct CalibrationProfile {
    std::size_t num_processors = 0;

    // sigma[processor][kernel_index(k)] = median solo seconds for kernel k
    // on core_for_processor(processor) (src/exec/affinity.hpp -- the exact
    // mapping a real Dispatcher run against this many processors uses).
    std::vector<std::array<double, kNumKernels>> sigma;

    // pi[kernel_index(reporter)][kernel_index(interferer)] = measured
    // contended seconds / sigma(reporter, probe_core_a), from one
    // representative pair of distinct cores. pi[k][k] is each kernel's
    // same-kernel self-interference ratio (e.g. two MEMORY_BOUND tasks
    // sharing LLC/DRAM bandwidth); off-diagonal entries are cross-kernel.
    // Identity (all 1.0) if the machine exposes fewer than 2 cores -- no
    // pair of distinct cores exists to measure contention on.
    std::array<std::array<double, kNumKernels>, kNumKernels> pi{};

    // comm[from][to] = median measured src/profile/comm_probe.hpp
    // handoff seconds from core_for_processor(from) to
    // core_for_processor(to). Sized num_processors x num_processors.
    // comm[p][p] is always 0 (no cross-core handoff exists within one
    // processor). If core_for_processor(from) == core_for_processor(to)
    // for from != to (an oversubscribed processor count, more logical
    // processors than physical cores, aliasing two of them onto the same
    // core), comm[from][to] is also 0 for the same reason -- documented,
    // not corrected for.
    std::vector<std::vector<double>> comm;

    bool all_pinned = true;

    // sigma(kernel, processor); throws std::out_of_range if processor is
    // outside [0, num_processors).
    double cost(KernelType kernel, std::size_t processor) const;

    // comm[from][to]; throws std::out_of_range if either index is outside
    // [0, num_processors).
    double comm_cost(std::size_t from, std::size_t to) const;

    // Mean of every off-diagonal comm[from][to] entry (from != to); 0.0 if
    // num_processors < 2 (no off-diagonal entries exist). See this
    // struct's header comment on why src/sched/eheft.hpp uses this single
    // scalar instead of the full matrix.
    double mean_comm_cost() const;
};

// Runs the full calibration sweep:
//   - sigma for every (kernel, processor) in [0, num_processors), each the
//     median of `reps` solo measurements via probe.hpp.
//   - pi for every (reporter, interferer) kernel pair, measured once on
//     cores (0, 1) (falls back to the identity, pi == 1 everywhere, if
//     std::thread::hardware_concurrency() reports fewer than 2 cores).
//   - comm for every (from, to) processor pair in [0, num_processors),
//     via comm_probe.hpp (0 on the diagonal and on any aliased-core pair,
//     per this struct's comm field comment).
// Real computation only (CLAUDE.md: no simulated durations) -- this
// function's wall-clock cost is proportional to
// num_processors * kNumKernels * reps solo passes, plus
// kNumKernels^2 * reps contended passes, plus
// num_processors^2 * reps handoff round-trips.
CalibrationProfile calibrate(std::size_t num_processors, const KernelConfig& cfg, int reps = 5, unsigned seed = 1);

}  // namespace heftsim::profile
