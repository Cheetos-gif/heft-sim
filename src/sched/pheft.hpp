// src/sched/pheft.hpp
//
// The confound-isolating control scheduler named but not built in
// REPORT.md §7 ("Per-processor-pair comm cost in eheft too... Isolating
// the two would need a fourth scheduler (contention-oblivious but
// per-pair-comm-aware) as a control"). `cheft` differs from `eheft` in
// two independent ways at once:
//   1. per-processor-pair comm cost (profile::CalibrationProfile::
//      comm_cost()) instead of eheft's single flat mean_comm_cost()
//      (eheft is forced through heft.cpp's one-scalar-per-edge Dag, see
//      eheft.hpp; cheft has its own insertion loop and isn't).
//   2. contention-aware EFT insertion (inflating a candidate's duration
//      by measured pi/pi3 against tentatively-overlapping co-runners,
//      see cheft.hpp).
// Comparing only eheft vs cheft conflates these: part of cheft's
// apparent edge in predicted-vs-measured error could be (1) alone, with
// (2) buying nothing. `pheft` (per-pair-comm HEFT) isolates (1): it is
// cheft's own EFT insertion loop -- same priority ranking, same
// per-processor-pair comm cost, same find_earliest_slot() -- with the
// contention multiplier forced to 1.0, i.e. (2) removed entirely.
//
//   eheft -> pheft   isolates the effect of per-pair comm cost alone.
//   pheft -> cheft   isolates the effect of contention-awareness alone,
//                    with the comm-cost model now held constant.
//
// pheft is not a new algorithm; it exists purely as a control arm for
// benchmark_suite.cpp's sweep.

#pragma once

#include "../dag/dag.hpp"
#include "../profile/calibration.hpp"
#include "heft.hpp"
#include "scheduler.hpp"

#include <string>

namespace heftsim {

// Same result shape as eheft_schedule()/cheft_schedule(). rank_u is the
// calibrated-but-contention-oblivious priority ranking (identical to
// what eheft and cheft both compute) -- pheft changes nothing about
// ranking, only EFT insertion's comm-cost lookup.
HeftResult pheft_schedule(const Dag& topology, const profile::CalibrationProfile& profile);

class PheftScheduler : public Scheduler {
public:
    explicit PheftScheduler(profile::CalibrationProfile profile) : profile_(std::move(profile)) {}

    std::string name() const override { return "pheft"; }

    Schedule schedule(const Dag& dag) const override {
        HeftResult r = pheft_schedule(dag, profile_);
        return Schedule{std::move(r.schedule), r.makespan};
    }

private:
    profile::CalibrationProfile profile_;
};

}  // namespace heftsim
