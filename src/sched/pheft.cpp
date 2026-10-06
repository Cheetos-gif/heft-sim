// src/sched/pheft.cpp
#include "pheft.hpp"

#include "eheft.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace heftsim {

namespace {

// Identical to heft.cpp's and cheft.cpp's own find_earliest_slot() --
// duplicated for the same reason cheft.cpp duplicates it (keep heft.cpp
// itself untouched as the unmodified reference baseline).
double find_earliest_slot(const std::vector<ScheduleEntry>& timeline, double ready, double duration) {
    double candidate = ready;
    for (const auto& e : timeline) {
        if (candidate + duration <= e.start) return candidate;
        candidate = std::max(candidate, e.finish);
    }
    return candidate;
}

}  // namespace

HeftResult pheft_schedule(const Dag& topology, const profile::CalibrationProfile& profile) {
    // Priority order from calibrated-but-contention-oblivious costs --
    // identical to what eheft and cheft both compute (pheft changes
    // nothing about ranking).
    Dag calibrated = calibrate_costs(topology, profile);
    std::vector<double> ranks = compute_upward_ranks(calibrated);

    const std::size_t n = topology.num_tasks();
    const std::size_t p = topology.num_processors();

    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    // Same epsilon-tolerant tie-break as heft.cpp's and cheft.cpp's sort.
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (std::fabs(ranks[a] - ranks[b]) > 1e-9) return ranks[a] > ranks[b];
        return a < b;
    });

    std::vector<std::vector<ScheduleEntry>> proc_timeline(p);
    std::vector<ScheduleEntry> assigned(n);

    for (int t : order) {
        const Task& task = topology.task(t);

        int best_proc = -1;
        double best_start = 0.0;
        double best_finish = std::numeric_limits<double>::infinity();

        for (std::size_t proc = 0; proc < p; ++proc) {
            double ready = 0.0;
            for (int pred : task.preds) {
                const ScheduleEntry& pe = assigned[pred];
                // Per-processor-pair comm cost -- same lookup cheft uses,
                // held constant here so the only difference against
                // cheft is the absence of a contention multiplier below.
                double c = (pe.processor == static_cast<int>(proc))
                               ? 0.0
                               : profile.comm_cost(static_cast<std::size_t>(pe.processor), proc);
                ready = std::max(ready, pe.finish + c);
            }

            double duration = profile.cost(task.kernel, proc);
            double start = find_earliest_slot(proc_timeline[proc], ready, duration);
            // No contention multiplier -- this is the control arm: a
            // candidate's finish time never accounts for tentatively-
            // overlapping co-runners on other processors (contrast
            // cheft_schedule()'s contention_multiplier() call here).
            double finish = start + duration;

            if (finish < best_finish) {
                best_finish = finish;
                best_start = start;
                best_proc = static_cast<int>(proc);
            }
        }

        ScheduleEntry entry{t, best_proc, best_start, best_finish};
        assigned[t] = entry;

        auto& tl = proc_timeline[best_proc];
        auto pos = std::upper_bound(tl.begin(), tl.end(), entry,
                                     [](const ScheduleEntry& a, const ScheduleEntry& b) { return a.start < b.start; });
        tl.insert(pos, entry);
    }

    double makespan = 0.0;
    for (const auto& e : assigned) makespan = std::max(makespan, e.finish);

    HeftResult result;
    result.schedule = std::move(assigned);
    result.rank_u = std::move(ranks);
    result.priority_order = std::move(order);
    result.makespan = makespan;
    return result;
}

}  // namespace heftsim
