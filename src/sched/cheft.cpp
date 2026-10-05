// src/sched/cheft.cpp
#include "cheft.hpp"

#include "eheft.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace heftsim {

namespace {

// Identical to heft.cpp's own find_earliest_slot() -- duplicated, not
// shared, to keep heft.cpp itself completely untouched (see cheft.hpp's
// header comment on why the reference implementation must stay
// contention-oblivious and unmodified).
double find_earliest_slot(const std::vector<ScheduleEntry>& timeline, double ready, double duration) {
    double candidate = ready;
    for (const auto& e : timeline) {
        if (candidate + duration <= e.start) return candidate;
        candidate = std::max(candidate, e.finish);
    }
    return candidate;
}

// Contention multiplier for `kernel` against whatever already-placed
// tasks tentatively overlap [start, finish) on any processor other than
// exclude_proc:
//   0 overlapping co-runners -> 1.0 (no inflation).
//   1 overlapping co-runner  -> measured pairwise cal.pi[kernel][co_kernel].
//   2 overlapping co-runners -> measured 3-way cal.pi3[kernel][combo] --
//     a genuine N-way measurement (src/profile/comm_probe.hpp's sibling,
//     src/profile/probe.hpp::measure_contended_multi), not an
//     approximation, for exactly the case that matters at
//     num_processors == 3 (the demo/benchmark default): the candidate's
//     two OTHER processors can together hold at most two overlapping
//     tasks.
//   >= 3 overlapping co-runners -> no direct measurement exists past
//     3-way; falls back to the maximum pairwise pi among all overlapping
//     co-runners as a conservative estimate (only reachable with
//     num_processors >= 5, not exercised by this project's current
//     fixtures/benchmarks, but handled so the function stays correct
//     for larger processor counts instead of silently under-counting).
double contention_multiplier(KernelType kernel, double start, double finish, int exclude_proc,
                              const Dag& topology, const std::vector<std::vector<ScheduleEntry>>& proc_timeline,
                              const profile::CalibrationProfile& cal) {
    std::vector<KernelType> co_kernels;
    for (std::size_t q = 0; q < proc_timeline.size(); ++q) {
        if (static_cast<int>(q) == exclude_proc) continue;
        for (const auto& e : proc_timeline[q]) {
            bool overlaps = e.start < finish && start < e.finish;
            if (overlaps) co_kernels.push_back(topology.task(e.task).kernel);
        }
    }

    std::size_t ki = profile::kernel_index(kernel);
    if (co_kernels.empty()) {
        return 1.0;
    }
    if (co_kernels.size() == 1) {
        return cal.pi[ki][profile::kernel_index(co_kernels[0])];
    }
    if (co_kernels.size() == 2) {
        return cal.pi3[ki][profile::combo_index(co_kernels[0], co_kernels[1])];
    }
    double worst = 1.0;
    for (KernelType co : co_kernels) {
        worst = std::max(worst, cal.pi[ki][profile::kernel_index(co)]);
    }
    return worst;
}

}  // namespace

HeftResult cheft_schedule(const Dag& topology, const profile::CalibrationProfile& profile) {
    // Priority order from calibrated-but-contention-oblivious costs --
    // identical to what an eheft run would compute (see cheft.hpp's
    // header comment on why contention correction only happens at
    // placement, never at ranking).
    Dag calibrated = calibrate_costs(topology, profile);
    std::vector<double> ranks = compute_upward_ranks(calibrated);

    const std::size_t n = topology.num_tasks();
    const std::size_t p = topology.num_processors();

    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    // Same epsilon-tolerant tie-break as heft.cpp's own sort.
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
                // Per-processor-pair comm cost, not heft.cpp's flat
                // per-edge scalar -- cheft's own insertion loop isn't
                // constrained by Dag's one-cost-per-edge representation
                // (see cheft.hpp's header comment and
                // profile::CalibrationProfile::comm's doc).
                double c = (pe.processor == static_cast<int>(proc))
                               ? 0.0
                               : profile.comm_cost(static_cast<std::size_t>(pe.processor), proc);
                ready = std::max(ready, pe.finish + c);
            }

            double base_duration = profile.cost(task.kernel, proc);
            double start = find_earliest_slot(proc_timeline[proc], ready, base_duration);

            double mult = contention_multiplier(task.kernel, start, start + base_duration, static_cast<int>(proc),
                                                 topology, proc_timeline, profile);
            double finish = start + base_duration * mult;

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
