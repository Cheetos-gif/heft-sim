// contention_pilot.cpp
//
// HEFT-Sim -- Phase 3 pilot experiment.
//
// Question: does a task's runtime depend on WHO ELSE is running, or only on
// which processor it was assigned to? HEFT assumes the latter.
//
// Method (reporter / interferer, after Mars et al., Bubble-Up, ASPLOS 2011):
//   - A "reporter" thread runs one measured pass of a kernel on core A.
//   - An "interferer" thread loops its kernel continuously on core B until
//     the reporter finishes. This guarantees the reporter is contended for
//     100% of its measured window (a plain "run both once" design does not:
//     the shorter task finishes early and the longer one runs partly solo).
//   - Slowdown = t_contended / t_solo.
//
// Two kernel classes:
//   COMPUTE : working set ~8 KB, fits in L1. Should be nearly immune.
//   MEMORY  : pointer chase over a 128 MB random cycle. Defeats the
//             prefetcher, every step is an LLC/DRAM miss. Should suffer.
//
// The COMPUTE/COMPUTE cell is the control. If it shows ~0% slowdown while
// MEMORY/MEMORY shows a large one, on the same cores in the same session,
// the effect cannot be blamed on OS noise or frequency scaling.
//
// Build:  g++ -O2 -pthread -o contention_pilot contention_pilot.cpp
// Run:    ./contention_pilot            (defaults)
//         ./contention_pilot 5 0 2      (reps, coreA, coreB)
//
// Pick two cores that are NOT SMT siblings for the headline numbers.
// Check with: lscpu -e     (look at the CORE column; two rows sharing a
// CORE value are siblings). Re-running on a sibling pair afterwards is a
// nice bonus result -- the slowdown usually gets dramatically worse.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <numeric>
#include <random>
#include <vector>

#include <pthread.h>
#include <sched.h>

// ---------------------------------------------------------------- tunables

static size_t MEM_BUF_BYTES = 128ull << 20;  // 128 MB, well past any L3
static size_t MEM_STEPS     = 20'000'000;    // dependent loads per pass
static size_t COMP_DOUBLES  = 1024;          // 8 KB -- lives in L1
static size_t COMP_ITERS    = 400'000;       // outer passes

enum Kernel { COMPUTE, MEMORY };
static const char* kname(Kernel k) { return k == COMPUTE ? "COMPUTE" : "MEMORY "; }

// ---------------------------------------------------------------- timing

static double now_sec() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);           // never wall clock
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ---------------------------------------------------------------- kernels

struct Workspace {
    std::vector<size_t> chase;   // MEMORY: index cycle
    std::vector<double> buf;     // COMPUTE: small hot array
    volatile double     sink = 0.0;
    volatile size_t     sink_i = 0;
};

// Build a single random Hamiltonian cycle over the buffer so that each load
// depends on the previous one. Sequential or strided access would just be
// prefetched and would show almost no contention.
static void init_memory_ws(Workspace& ws, unsigned seed) {
    size_t n = MEM_BUF_BYTES / sizeof(size_t);
    std::vector<size_t> perm(n);
    std::iota(perm.begin(), perm.end(), 0);
    std::mt19937_64 rng(seed);
    std::shuffle(perm.begin() + 1, perm.end(), rng);

    ws.chase.assign(n, 0);
    for (size_t i = 0; i + 1 < n; ++i) ws.chase[perm[i]] = perm[i + 1];
    ws.chase[perm[n - 1]] = perm[0];
}

static void init_compute_ws(Workspace& ws, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> d(0.5, 1.5);
    ws.buf.resize(COMP_DOUBLES);
    for (auto& v : ws.buf) v = d(rng);
}

static void run_memory(Workspace& ws) {
    size_t idx = 0;
    for (size_t i = 0; i < MEM_STEPS; ++i) idx = ws.chase[idx];
    ws.sink_i = idx;                                // defeat dead-code removal
}

static void run_compute(Workspace& ws) {
    double acc = 1.0;
    const size_t n = ws.buf.size();
    for (size_t it = 0; it < COMP_ITERS; ++it)
        for (size_t i = 0; i < n; ++i)
            acc = acc * 0.9999 + ws.buf[i] * 0.0001;
    ws.sink = acc;
}

static void run_once(Kernel k, Workspace& ws) {
    if (k == MEMORY) run_memory(ws); else run_compute(ws);
}

// ---------------------------------------------------------------- threads

static bool pin(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

struct ReporterArg {
    int                core;
    Kernel             k;
    Workspace*         ws;
    pthread_barrier_t* bar;
    double             elapsed = 0.0;
    bool               pinned  = false;
};

struct InterfererArg {
    int                core;
    Kernel             k;
    Workspace*         ws;
    pthread_barrier_t* bar;
    std::atomic<bool>* stop;
    bool               pinned = false;
};

static void* reporter_fn(void* p) {
    auto* a = static_cast<ReporterArg*>(p);
    a->pinned = pin(a->core);
    pthread_barrier_wait(a->bar);
    double t0 = now_sec();
    run_once(a->k, *a->ws);
    a->elapsed = now_sec() - t0;
    return nullptr;
}

static void* interferer_fn(void* p) {
    auto* a = static_cast<InterfererArg*>(p);
    a->pinned = pin(a->core);
    pthread_barrier_wait(a->bar);
    while (!a->stop->load(std::memory_order_relaxed)) run_once(a->k, *a->ws);
    return nullptr;
}

// One measured pass of `rep` on coreA. If `has_int`, `intk` loops on coreB
// for the whole window. Workspaces are per-thread: two MEMORY threads must
// NOT share a buffer, or you would be measuring sharing, not contention.
static double measure(Kernel rep, int coreA, bool has_int, Kernel intk, int coreB) {
    Workspace ws_r;
    (rep == MEMORY) ? init_memory_ws(ws_r, 1) : init_compute_ws(ws_r, 1);

    Workspace ws_i;
    if (has_int) (intk == MEMORY) ? init_memory_ws(ws_i, 2) : init_compute_ws(ws_i, 2);

    // Touch everything so page faults land outside the measured window.
    run_once(rep, ws_r);

    pthread_barrier_t bar;
    pthread_barrier_init(&bar, nullptr, has_int ? 2 : 1);
    std::atomic<bool> stop{false};

    ReporterArg ra{coreA, rep, &ws_r, &bar};
    InterfererArg ia{coreB, intk, &ws_i, &bar, &stop};

    pthread_t tr, ti;
    if (has_int) pthread_create(&ti, nullptr, interferer_fn, &ia);
    pthread_create(&tr, nullptr, reporter_fn, &ra);

    pthread_join(tr, nullptr);
    if (has_int) { stop.store(true); pthread_join(ti, nullptr); }
    pthread_barrier_destroy(&bar);

    if (!ra.pinned || (has_int && !ia.pinned))
        fprintf(stderr, "  [warn] affinity failed -- numbers are unreliable\n");

    return ra.elapsed;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static double run_reps(int reps, Kernel rep, int cA, bool hi, Kernel ik, int cB) {
    std::vector<double> t;
    for (int i = 0; i < reps; ++i) t.push_back(measure(rep, cA, hi, ik, cB));
    return median(t);
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    int reps  = (argc > 1) ? atoi(argv[1]) : 5;
    int coreA = (argc > 2) ? atoi(argv[2]) : 0;
    int coreB = (argc > 3) ? atoi(argv[3]) : 2;

    printf("HEFT-Sim contention pilot\n");
    printf("reps=%d (median reported)  reporter=core %d  interferer=core %d\n",
           reps, coreA, coreB);
    printf("MEMORY  working set: %.0f MB, %zu dependent loads\n",
           MEM_BUF_BYTES / 1048576.0, MEM_STEPS);
    printf("COMPUTE working set: %.0f KB, %zu passes\n\n",
           COMP_DOUBLES * sizeof(double) / 1024.0, COMP_ITERS);

    const Kernel ks[2] = {COMPUTE, MEMORY};

    double solo[2];
    for (int i = 0; i < 2; ++i) {
        solo[i] = run_reps(reps, ks[i], coreA, false, COMPUTE, coreB);
        printf("solo  %s : %7.3f s\n", kname(ks[i]), solo[i]);
    }

    printf("\n  reporter   co-runner    solo      paired   slowdown\n");
    printf("  ---------------------------------------------------\n");
    double sd[2][2];
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            double t = run_reps(reps, ks[r], coreA, true, ks[c], coreB);
            sd[r][c] = t / solo[r];
            printf("  %s    %s  %7.3f s %7.3f s   %5.2fx  (%+.1f%%)\n",
                   kname(ks[r]), kname(ks[c]), solo[r], t, sd[r][c],
                   (sd[r][c] - 1.0) * 100.0);
        }
    }

    printf("\nSlowdown matrix (rows = measured task, cols = co-runner):\n");
    printf("               COMPUTE   MEMORY\n");
    for (int r = 0; r < 2; ++r)
        printf("  %s       %5.2fx   %5.2fx\n", kname(ks[r]), sd[r][0], sd[r][1]);

    printf("\nHEFT assumes every cell in this matrix is 1.00x.\n");
    printf("Control: COMPUTE/COMPUTE should be ~1.00x. If it is, the other\n");
    printf("cells cannot be attributed to OS noise or frequency scaling.\n");
    return 0;
}
