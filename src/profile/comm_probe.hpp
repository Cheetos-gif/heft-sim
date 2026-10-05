// src/profile/comm_probe.hpp
//
// Measures real cross-core data-handoff latency, the missing half of
// src/sched/eheft.hpp's calibration (previously every edge's comm_cost
// was hardcoded 0 -- see calibrate_costs()'s old comment -- because
// nothing measured it). Method: a producer thread pinned to
// producer_core writes a small payload and publishes a sequence number
// via a release store; a consumer thread pinned to consumer_core
// spin-waits on an acquire load of that sequence number, then
// timestamps. Elapsed = consumer's observed-ready time minus producer's
// pre-write time, median over `reps`.
//
// This models one task handing a small result to another (the common
// case for the kernels in src/kernels/hpp, which produce a scalar
// accumulator or a chase index, not a bulk buffer) -- not large bulk
// data movement. Documented limitation, not a general-purpose memory
// bandwidth benchmark.
//
// Synchronization is busy-wait spinning, not condition_variable/mutex:
// at the sub-microsecond latencies this measures, OS wake latency would
// swamp the real handoff cost being measured. This is the standard
// technique for this class of ping-pong microbenchmark (e.g. lmbench's
// own inter-core latency tests), not "sleep() as work" -- every spin
// iteration is a genuine atomic load, and the thing being timed (the
// producer's write-to-consumer's-observed-store latency) is real,
// physical cache-coherence traffic, never a simulated duration.

#pragma once

namespace heftsim::profile {

struct CommProbeResult {
    double seconds = 0.0;  // median over reps
    bool pinned = true;    // AND of both threads' pin success across every rep
};

// producer_core and consumer_core must be distinct -- throws
// std::invalid_argument otherwise (same discipline as
// src/profile/probe.hpp's measure_contended).
CommProbeResult measure_handoff(int producer_core, int consumer_core, int reps, unsigned seed = 1);

}  // namespace heftsim::profile
