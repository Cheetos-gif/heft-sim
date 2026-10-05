// src/profile/pressure.hpp
//
// Background contention load for src/profile/probe.*: a thread pinned to
// one core, looping a real kernel pass (src/kernels/kernel.hpp) with no
// pause between passes, until told to stop. This is contention_pilot.cpp's
// interferer_fn generalized into a reusable component instead of a
// one-shot main()-local struct, so probe.cpp can spin one up per
// measurement without duplicating the thread-lifecycle logic.
//
// CLAUDE.md invariants this preserves: pinned via src/exec/affinity.hpp
// (the only place allowed to call the pthread affinity API), real
// computation every pass (never sleeps, never fakes elapsed time).

#pragma once

#include "../kernels/kernel.hpp"

#include <atomic>
#include <thread>

namespace heftsim::profile {

class PressureGenerator {
public:
    // Does not start the background thread; call start() once init_workspace
    // has had a chance to run (see start()'s own comment on first-fault
    // cost). `seed` must differ from any other Workspace alive on another
    // core during the same measurement, or a MEMORY_BOUND kernel's
    // pointer-chase could alias another thread's chase (see
    // src/kernels/kernel.hpp's never-share-a-Workspace rule -- a shared
    // *seed* on two independently-allocated Workspaces is fine, it just
    // produces the same permutation twice; this is about not aliasing the
    // same std::vector, which separate Workspace instances already avoid).
    PressureGenerator(KernelType kernel, int core, KernelConfig cfg, unsigned seed);

    // Stops (if still running) and joins before destruction -- never leaves
    // a detached thread touching ws_ after this object is gone.
    ~PressureGenerator();

    PressureGenerator(const PressureGenerator&) = delete;
    PressureGenerator& operator=(const PressureGenerator&) = delete;

    // Allocates/touches the workspace (first-fault cost paid here, outside
    // any timed window -- mirrors src/kernels/kernel.hpp's init_workspace
    // contract) and spawns the background thread. Must be called at most
    // once per instance.
    void start();

    // Signals the loop to stop after its current in-flight kernel pass
    // completes (never mid-pass -- every pass is genuine, uninterrupted
    // work) and joins the thread. Idempotent: a second call is a no-op.
    // Safe to skip; the destructor calls it if the caller didn't.
    void stop();

    // Whether the background thread's pin_current_thread() call succeeded.
    // Only meaningful after start(); a failed pin means this generator's
    // contention is contaminated by migration and callers should treat the
    // resulting measurement as unreliable, not fatal (same tradeoff as
    // every other pinning call site in this codebase).
    bool pinned() const { return pinned_.load(std::memory_order_relaxed); }

private:
    void loop();

    KernelType kernel_;
    int core_;
    KernelConfig cfg_;
    unsigned seed_;

    Workspace ws_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> pinned_{false};
    std::thread thread_;
    bool started_ = false;
};

}  // namespace heftsim::profile
