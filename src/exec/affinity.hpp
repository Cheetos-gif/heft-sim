// src/exec/affinity.hpp
//
// Thin wrapper over pthread_setaffinity_np. CLAUDE.md invariant: every
// worker thread is pinned to an explicit core -- this is the only place
// that's allowed to call the pthread affinity API.

#pragma once

namespace heftsim {

// Pins the calling thread to `core`. Returns false (and leaves the
// thread's affinity unchanged, per pthread semantics on failure) if the
// underlying call failed -- e.g. `core` is out of range for this machine.
// Callers must not treat this as fatal on its own: a failed pin means
// timings for that thread's tasks are unreliable, not that execution is
// unsafe to continue (see contention_pilot.cpp's identical tradeoff).
bool pin_current_thread(int core);

// Centralized processor -> core mapping used by every component that
// either executes a Dag's tasks (src/exec/dispatcher.cpp) or needs to
// measure costs on the exact cores a real Dispatcher run would use
// (src/profile/calibration.cpp): core = processor % hardware_concurrency().
// Kept in one place so the two can never drift apart.
int core_for_processor(int processor);

}  // namespace heftsim
