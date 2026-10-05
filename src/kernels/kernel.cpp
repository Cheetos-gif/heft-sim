// src/kernels/kernel.cpp
#include "kernel.hpp"

#include <algorithm>
#include <numeric>
#include <random>

namespace heftsim {

namespace {

void init_memory(Workspace& ws, unsigned seed, const KernelConfig& cfg) {
    std::size_t n = cfg.mem_buf_bytes / sizeof(std::size_t);
    if (n < 2) n = 2;
    std::vector<std::size_t> perm(n);
    std::iota(perm.begin(), perm.end(), 0);
    std::mt19937_64 rng(seed);
    std::shuffle(perm.begin() + 1, perm.end(), rng);

    ws.chase.assign(n, 0);
    for (std::size_t i = 0; i + 1 < n; ++i) ws.chase[perm[i]] = perm[i + 1];
    ws.chase[perm[n - 1]] = perm[0];
}

void init_compute(Workspace& ws, unsigned seed, const KernelConfig& cfg) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> d(0.5, 1.5);
    ws.buf.resize(cfg.comp_doubles);
    for (auto& v : ws.buf) v = d(rng);
}

// Steps per chunk: total work divided into cfg.heartbeat_chunks roughly
// equal pieces, at least 1 so a chunk always makes forward progress.
std::size_t chunk_size(std::size_t total, std::size_t chunks) {
    if (chunks == 0) chunks = 1;
    std::size_t per = total / chunks;
    if (total % chunks != 0) ++per;
    return per == 0 ? 1 : per;
}

bool run_memory_chunk(Workspace& ws, const KernelConfig& cfg, KernelProgress& progress) {
    std::size_t step = chunk_size(cfg.mem_steps, cfg.heartbeat_chunks);
    std::size_t remaining = cfg.mem_steps - progress.steps_done;
    std::size_t this_chunk = std::min(step, remaining);

    std::size_t idx = progress.chase_idx;
    for (std::size_t i = 0; i < this_chunk; ++i) idx = ws.chase[idx];
    progress.chase_idx = idx;
    progress.steps_done += this_chunk;

    bool done = progress.steps_done >= cfg.mem_steps;
    if (done) ws.sink_i = idx;  // defeat dead-code removal
    return done;
}

bool run_compute_chunk(Workspace& ws, const KernelConfig& cfg, KernelProgress& progress) {
    std::size_t step = chunk_size(cfg.comp_iters, cfg.heartbeat_chunks);
    std::size_t remaining = cfg.comp_iters - progress.iters_done;
    std::size_t this_chunk = std::min(step, remaining);

    double acc = progress.compute_acc;
    const std::size_t n = ws.buf.size();
    for (std::size_t it = 0; it < this_chunk; ++it)
        for (std::size_t i = 0; i < n; ++i) acc = acc * 0.9999 + ws.buf[i] * 0.0001;
    progress.compute_acc = acc;
    progress.iters_done += this_chunk;

    bool done = progress.iters_done >= cfg.comp_iters;
    if (done) ws.sink = acc;
    return done;
}

}  // namespace

void init_workspace(KernelType kernel, Workspace& ws, unsigned seed, const KernelConfig& cfg) {
    if (kernel == KernelType::MEMORY_BOUND) {
        init_memory(ws, seed, cfg);
    } else {
        init_compute(ws, seed, cfg);
    }
}

bool run_kernel_chunk(KernelType kernel, Workspace& ws, const KernelConfig& cfg, KernelProgress& progress) {
    if (kernel == KernelType::MEMORY_BOUND) {
        return run_memory_chunk(ws, cfg, progress);
    }
    return run_compute_chunk(ws, cfg, progress);
}

void run_kernel(KernelType kernel, Workspace& ws, const KernelConfig& cfg) {
    KernelProgress progress;
    while (!run_kernel_chunk(kernel, ws, cfg, progress)) {
    }
}

}  // namespace heftsim
