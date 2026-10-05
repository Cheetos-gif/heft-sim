// src/sched/heft.cpp
#include "heft.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace heftsim {

namespace {

double average_cost(const Task& t) {
    return std::accumulate(t.cost.begin(), t.cost.end(), 0.0) / static_cast<double>(t.cost.size());
}

enum class Mark : char { UNVISITED, VISITING, DONE };

double rank_u_of(int id, const Dag& dag, std::vector<double>& memo, std::vector<Mark>& mark) {
    if (mark[id] == Mark::DONE) return memo[id];
    if (mark[id] == Mark::VISITING) {
        throw std::runtime_error("compute_upward_ranks: cycle detected at task " + std::to_string(id));
    }
    mark[id] = Mark::VISITING;

    double best_succ = 0.0;
    for (int s : dag.successors(id)) {
        double via_s = dag.comm_cost(id, s) + rank_u_of(s, dag, memo, mark);
        best_succ = std::max(best_succ, via_s);
    }

    double r = average_cost(dag.task(id)) + best_succ;
    memo[id] = r;
    mark[id] = Mark::DONE;
    return r;
}

// Earliest start time >= ready that fits `duration` into `timeline`
// (sorted ascending by start, non-overlapping). Insertion-based: probes
// the idle gaps between already-scheduled entries, not just the tail.
double find_earliest_slot(const std::vector<ScheduleEntry>& timeline, double ready, double duration) {
    double candidate = ready;
    for (const auto& e : timeline) {
        if (candidate + duration <= e.start) return candidate;
        candidate = std::max(candidate, e.finish);
    }
    return candidate;
}

}  // namespace

std::vector<double> compute_upward_ranks(const Dag& dag) {
    std::size_t n = dag.num_tasks();
    std::vector<double> memo(n, 0.0);
    std::vector<Mark> mark(n, Mark::UNVISITED);
    for (std::size_t i = 0; i < n; ++i) {
        rank_u_of(static_cast<int>(i), dag, memo, mark);
    }
    return memo;
}

HeftResult heft_schedule(const Dag& dag) {
    const std::size_t n = dag.num_tasks();
    const std::size_t p = dag.num_processors();

    std::vector<double> ranks = compute_upward_ranks(dag);

    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    // rank_u is a sum of divisions (average costs); mathematically-tied
    // ranks (e.g. two paths that both work out to exactly 80) can land a
    // few ULPs apart in double. Compare with a small epsilon so such
    // ties fall back to the ascending-id rule instead of float noise.
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        constexpr double kRankEps = 1e-6;
        double diff = ranks[a] - ranks[b];
        if (std::fabs(diff) > kRankEps) return diff > 0;
        return a < b;  // deterministic tie-break
    });

    std::vector<std::vector<ScheduleEntry>> proc_timeline(p);
    std::vector<ScheduleEntry> assigned(n);

    for (int t : order) {
        const Task& task = dag.task(t);

        int best_proc = -1;
        double best_start = 0.0;
        double best_finish = std::numeric_limits<double>::infinity();

        for (std::size_t proc = 0; proc < p; ++proc) {
            double ready = 0.0;
            for (int pred : task.preds) {
                const ScheduleEntry& pe = assigned[pred];
                double comm = (pe.processor == static_cast<int>(proc)) ? 0.0 : dag.comm_cost(pred, t);
                ready = std::max(ready, pe.finish + comm);
            }
            double duration = task.cost[proc];
            double start = find_earliest_slot(proc_timeline[proc], ready, duration);
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
