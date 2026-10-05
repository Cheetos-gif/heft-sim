// src/sched/cheft.hpp
//
// Contention-aware HEFT: the research contribution this project exists
// to test (CLAUDE.md: "HEFT's cost model w(task, processor) ignores
// co-runner interference; we measure that error and correct for it").
//
// Priority ordering (rank_u) is computed exactly like
// src/sched/eheft.hpp -- against calibrated, but still
// contention-oblivious, costs -- because no principled contention
// estimate exists before any task has been placed; rank_u is a
// pre-placement priority, so it has nothing to condition on yet.
//
// The correction happens only at EFT insertion: for each candidate
// processor, cheft inflates a task's base (solo, sigma) duration by the
// worst measured pi multiplier (src/profile/calibration.hpp) against
// whatever already-placed tasks on OTHER processors tentatively overlap
// its execution window, then picks the processor with the smallest
// *inflated* finish time instead of src/sched/heft.cpp's uninflated one.
// heft.cpp itself is not modified -- cheft.cpp duplicates its small
// find_earliest_slot() gap-finder rather than touching the reference
// implementation (same reasoning as src/sched/eheft.hpp leaving it
// alone: it must stay the unmodified contention-oblivious baseline).
//
// Known approximations -- documented, not hidden; measuring exactly how
// far this falls short of real contention is the point of this project:
//   - Only already-placed (higher-priority) tasks are visible when
//     evaluating a candidate's contention. A lower-priority task placed
//     *later* that ends up temporally overlapping is invisible at
//     decision time -- the same greedy, never-revisit-a-placement
//     limitation HEFT's own EFT insertion already has for cost.
//   - pi is only ever measured pairwise -- one reporter, one interferer
//     (src/profile/comm_probe.hpp's methodology, after Mars et al.,
//     Bubble-Up). With more than one concurrent co-runner, cheft takes
//     the *maximum* pairwise pi across every overlapping co-runner as a
//     single conservative estimate -- not a validated N-way interference
//     model.
//   - A task's inflated duration is computed only *after*
//     find_earliest_slot() has already picked a start time using the
//     uninflated (base, sigma) duration. The inflated window is never
//     re-checked against that processor's own later-placed timeline
//     entries. Rare in practice, and consistent with this project's own
//     position that a *predicted* schedule and a *measured* one are
//     expected to diverge (CLAUDE.md: "Predicted vs measured is the
//     primary metric").

#pragma once

#include "../dag/dag.hpp"
#include "../profile/calibration.hpp"
#include "heft.hpp"
#include "scheduler.hpp"

#include <string>

namespace heftsim {

// Same result shape as heft_schedule()/eheft_schedule() -- contention
// correction is purely an EFT insertion-time adjustment; nothing else
// about HEFT's output shape changes. rank_u in the result is the
// calibrated-but-contention-oblivious priority ranking actually used to
// order placement, not a contention-corrected rank (see this header's
// comment on why rank_u can't be contention-aware).
HeftResult cheft_schedule(const Dag& topology, const profile::CalibrationProfile& profile);

class CheftScheduler : public Scheduler {
public:
    explicit CheftScheduler(profile::CalibrationProfile profile) : profile_(std::move(profile)) {}

    std::string name() const override { return "cheft"; }

    Schedule schedule(const Dag& dag) const override {
        HeftResult r = cheft_schedule(dag, profile_);
        return Schedule{std::move(r.schedule), r.makespan};
    }

private:
    profile::CalibrationProfile profile_;
};

}  // namespace heftsim
