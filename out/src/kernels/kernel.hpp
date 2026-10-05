// src/kernels/kernel.hpp
//
// The real work a Task performs, keyed off Task::kernel (KernelType). Both
// kernels are lifted from contention_pilot.cpp's COMPUTE/MEMORY pair:
//   COMPUTE_BOUND : a small buffer that fits in L1 -- dominated by ALU
//                   throughput, nearly immune to co-runner contention.
//   MEMORY_BOUND  : a random pointer-chase over a buffer sized to defeat
//                   the prefetcher -- every step is a cache/DRAM miss.
// Sizes are configurable (KernelConfig) rather than contention_pilot's
// static tunables so the executor can run a full DAG in a demo-sized
// window without touching this file.

#pragma once

#include "../dag/task.hpp"

#include <cstddef>
#include <vector>

namespace heftsim {

struct KernelConfig {
    std::size_t mem_buf_bytes = 8ull << 20;  // pointer-chase buffer size
    std::size_t mem_steps = 2'000'000;       // dependent loads per pass
    std::size_t comp_doubles = 1024;         // hot buffer size (8 KB)
    std::size_t comp_iters = 50'000;         // outer passes
    // Splits one kernel pass into this many resumable slices so a caller
    // (src/exec/dispatcher.cpp) can publish a heartbeat between slices
    // without changing the total amount of work done. 1 = no chunking,
    // identical to the original single-call behavior.
    std::size_t heartbeat_chunks = 1;
};

// Private per-task scratch space. Never share one Workspace across tasks
// or threads: a MEMORY_BOUND workspace is a pointer-chase sized to defeat
// the prefetcher, and two threads walking the same chase concurrently
// would measure sharing, not either task's own cost (see
// contention_pilot.cpp's identical warning).
struct Workspace {
    std::vector<std::size_t> chase;  // MEMORY_BOUND: index cycle
    std::vector<double> buf;         // COMPUTE_BOUND: small hot array
    volatile double sink = 0.0;
    volatile std::size_t sink_i = 0;
};

// Resumable progress for one execution attempt of one kernel pass. A
// fresh KernelProgress must be used per attempt -- never shared across
// attempts or tasks (mirrors Workspace's own never-share rule).
struct KernelProgress {
    std::size_t steps_done = 0;    // MEMORY_BOUND: dependent loads completed
    std::size_t chase_idx = 0;     // MEMORY_BOUND: current position
    std::size_t iters_done = 0;    // COMPUTE_BOUND: outer passes completed
    double compute_acc = 1.0;      // COMPUTE_BOUND: running accumulator
};

// Allocates and fills ws for `kernel`. Call this *outside* any timed
// window -- it touches every page, and first-fault cost has no business
// polluting a task's measured execution time.
void init_workspace(KernelType kernel, Workspace& ws, unsigned seed, const KernelConfig& cfg);

// Runs roughly 1/cfg.heartbeat_chunks of the kernel's total configured
// work, resuming from `progress`'s saved position and mutating it in
// place. Returns true once the kernel's *entire* configured workload has
// been completed across however many chunk calls it took -- the caller
// must stop calling after that. Every chunk does genuine computation
// (never sleeps, never fakes elapsed time); only the granularity at which
// a caller can observe progress changes, not the amount of real work.
bool run_kernel_chunk(KernelType kernel, Workspace& ws, const KernelConfig& cfg, KernelProgress& progress);

// Runs the kernel to completion in one call: equivalent to looping
// run_kernel_chunk with a fresh KernelProgress until it returns true.
// This is the only call that should happen inside a timed window when a
// caller doesn't need heartbeat granularity.
void run_kernel(KernelType kernel, Workspace& ws, const KernelConfig& cfg);

}  // namespace heftsim
