// src/sched/eheft.hpp
//
// Empirical HEFT: the exact algorithm in src/sched/heft.hpp (same rank_u /
// EFT insertion, src/sched/heft.cpp is not duplicated or modified), but
// Task::cost comes from a src/profile/calibration.hpp CalibrationProfile
// -- this machine's real measured solo kernel passes -- instead of a
// hand-written cost table. "Empirical" describes where the numbers came
// from, not a different scheduling algorithm: this is still the same
// contention-oblivious HEFT, calibrated. It exists so 'predicted' and
// 'measured' share real units (seconds) for *any* Dag, not just the one
// hardcoded fixture tests/fixtures/topcuoglu_calibrated.cpp originally
// special-cased.
//
// eheft is the fair baseline src/sched/cheft.hpp is meant to beat: if
// cheft's predicted-vs-measured error isn't smaller than eheft's on the
// same workload, contention-awareness bought nothing.

#pragma once

#include "../dag/dag.hpp"
#include "../profile/calibration.hpp"
#include "heft.hpp"
#include "scheduler.hpp"

#include <string>

namespace heftsim {

// Builds a copy of `topology` with the same tasks (preds, KernelType) and
// the same edges (from/to pairs), but every Task::cost[p] replaced by
// profile.cost(task.kernel, p), and every edge's comm_cost replaced by
// profile.mean_comm_cost() -- a single representative cross-processor
// handoff cost (src/profile/comm_probe.hpp), not a processor-pair-
// specific one, because src/sched/heft.cpp's own cost model takes one
// flat comm_cost per edge, never a matrix. `topology`'s own (possibly
// nonzero, possibly paper-unit) edge costs are discarded entirely, not
// read.
//
// Throws std::invalid_argument if topology.num_processors() !=
// profile.num_processors (a profile calibrated for a different processor
// count cannot be applied here -- see profile::CalibrationProfile::cost).
Dag calibrate_costs(const Dag& topology, const profile::CalibrationProfile& profile);

// calibrate_costs(topology, profile) followed by heft_schedule() on the
// result. Returns the same HeftResult shape as heft_schedule() --
// rank_u/priority_order are computed against the *calibrated* cost table.
HeftResult eheft_schedule(const Dag& topology, const profile::CalibrationProfile& profile);

// Scheduler-interface adapter. Holds its own CalibrationProfile (produced
// once by profile::calibrate() and reused across schedule() calls --
// recalibrating per call would be both slow and pointless, since the
// profile doesn't depend on which Dag is being scheduled).
class EheftScheduler : public Scheduler {
public:
    explicit EheftScheduler(profile::CalibrationProfile profile) : profile_(std::move(profile)) {}

    std::string name() const override { return "eheft"; }

    Schedule schedule(const Dag& dag) const override {
        HeftResult r = eheft_schedule(dag, profile_);
        return Schedule{std::move(r.schedule), r.makespan};
    }

private:
    profile::CalibrationProfile profile_;
};

}  // namespace heftsim
