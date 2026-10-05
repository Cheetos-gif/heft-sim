#!/usr/bin/env bash
# run_demo.sh
#
# Packaging only: builds every existing binary from clean and runs the
# demo sequence in order. Touches zero scheduling/execution/kernel/FT
# logic -- see CLAUDE.md and the individual source files for that. This
# script only builds, sequences, headers, and (for stage 5) derives a
# latency number from two already-printed timestamps.
#
# Requires: g++ with C++17 + pthread + POSIX (pthread_setaffinity_np,
# clock_gettime) support -- i.e. a real Linux environment (or WSL on
# Windows; the native MinGW toolchain on this repo's dev machine lacks
# pthread.h/sched.h entirely).
#
# Usage: ./run_demo.sh

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Isrc"
CXXFLAGS_PTHREAD="${CXXFLAGS} -pthread"

# Force line-buffered stdout/stderr regardless of whether this script's
# own output is redirected/piped (e.g. through `tee` to save a transcript
# while narrating live) -- C stdio only auto-line-buffers on a real TTY,
# and without this a multi-minute stage like contention_pilot would dump
# everything at once at exit instead of streaming as it runs.
if command -v stdbuf >/dev/null 2>&1; then
    RUN="stdbuf -oL -eL"
else
    RUN=""
fi

header() {
    echo
    echo "================================================================"
    echo "  $1"
    echo "================================================================"
}

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "FATAL: no C++ compiler found (\$CXX=$CXX). Set CXX or install g++." >&2
    exit 1
fi

# ---------------------------------------------------------------- build

header "BUILD: cleaning and compiling everything from scratch"

rm -f contention_pilot
rm -f tests/fixtures/topcuoglu tests/fixtures/naive tests/fixtures/topcuoglu_exec tests/fixtures/topcuoglu_ft tests/fixtures/topcuoglu_calibrated
rm -f tests/fixtures/*.exe
rm -rf out

echo "-- contention_pilot"
$CXX $CXXFLAGS_PTHREAD -o contention_pilot contention_pilot.cpp

echo "-- tests/fixtures/topcuoglu (HEFT paper-fixture regression)"
$CXX $CXXFLAGS -o tests/fixtures/topcuoglu \
    tests/fixtures/topcuoglu.cpp src/dag/dag.cpp src/sched/heft.cpp

echo "-- tests/fixtures/naive (round-robin baseline regression)"
$CXX $CXXFLAGS -o tests/fixtures/naive \
    tests/fixtures/naive.cpp src/dag/dag.cpp src/sched/naive.cpp

echo "-- tests/fixtures/topcuoglu_exec (naive vs HEFT, predicted+measured)"
$CXX $CXXFLAGS_PTHREAD -o tests/fixtures/topcuoglu_exec \
    tests/fixtures/topcuoglu_exec.cpp \
    src/dag/dag.cpp src/sched/heft.cpp src/sched/naive.cpp \
    src/kernels/kernel.cpp src/exec/affinity.cpp src/exec/event_log.cpp \
    src/exec/heartbeat.cpp src/exec/dispatcher.cpp \
    src/ft/fault_injector.cpp src/ft/heartbeat_monitor.cpp

echo "-- tests/fixtures/topcuoglu_ft (fault injection + recovery demo)"
$CXX $CXXFLAGS_PTHREAD -o tests/fixtures/topcuoglu_ft \
    tests/fixtures/topcuoglu_ft.cpp \
    src/dag/dag.cpp src/sched/heft.cpp \
    src/kernels/kernel.cpp src/exec/affinity.cpp src/exec/event_log.cpp \
    src/exec/heartbeat.cpp src/exec/dispatcher.cpp \
    src/ft/fault_injector.cpp src/ft/heartbeat_monitor.cpp

echo "-- tests/fixtures/topcuoglu_calibrated (sigma/pi calibration, unit-matched error%)"
$CXX $CXXFLAGS_PTHREAD -o tests/fixtures/topcuoglu_calibrated \
    tests/fixtures/topcuoglu_calibrated.cpp \
    src/dag/dag.cpp src/sched/heft.cpp src/sched/naive.cpp \
    src/kernels/kernel.cpp src/exec/affinity.cpp src/exec/event_log.cpp \
    src/exec/heartbeat.cpp src/exec/dispatcher.cpp \
    src/ft/fault_injector.cpp src/ft/heartbeat_monitor.cpp \
    src/profile/pressure.cpp src/profile/probe.cpp src/profile/calibration.cpp

echo "build OK"

# ------------------------------------------------------------- stage 1

header "STAGE 1/6: contention pilot (10 reps) -- does contention exist at all?"
$RUN ./contention_pilot 10

# ------------------------------------------------------------- stage 2

header "STAGE 2/6: Topcuoglu HEFT test suite (paper fixture regression)"
if $RUN ./tests/fixtures/topcuoglu; then
    echo "RESULT: PASS -- HEFT matches the published paper's ranks/schedule/makespan"
else
    echo "RESULT: FAIL -- HEFT disagrees with the published paper"
    exit 1
fi

# --------------------------------------------------------- stages 3 & 4
#
# One binary (topcuoglu_exec) computes each scheduler's predicted
# schedule and then executes it for real, printing both numbers in the
# same final table -- there is no separate "predicted-only" build to run.
# Stages 3 and 4 are therefore two narration beats around one execution,
# not two separate runs (packaging only -- this script does not split or
# alter what that binary computes or prints).

header "STAGE 3/6: naive vs HEFT -- predicted makespan for both"
echo "(predicted numbers appear in the 'predicted' column of the table below)"

header "STAGE 4/6: real-thread execution -- measured makespan"
echo "(same run as stage 3; 'measured(s)' column below is real pinned-thread execution)"
echo
echo "NOTE: predicted is in paper cost-units, measured is in seconds;"
echo "unit-matched calibration is stage 6 (topcuoglu_calibrated), not this one."
echo
$RUN ./tests/fixtures/topcuoglu_exec

# ------------------------------------------------------------- stage 5

header "STAGE 5/6: fault injection -- kill, FAILURE_DETECTED, recovery"
ft_rc=0
ft_output="$($RUN ./tests/fixtures/topcuoglu_ft)" || ft_rc=$?
echo "$ft_output"

t_fail="$(echo "$ft_output" | awk '/FAILURE_DETECTED/{print $2; exit}')"
t_recover="$(echo "$ft_output" | awk '/RECOVERY_COMPLETE/{print $2; exit}')"
if [[ -n "$t_fail" && -n "$t_recover" ]]; then
    latency_ms="$(awk -v a="$t_fail" -v b="$t_recover" 'BEGIN{printf "%.2f", (b-a)*1000}')"
    echo
    echo "Recovery latency: FAILURE_DETECTED (t=${t_fail}s) -> RECOVERY_COMPLETE (t=${t_recover}s) = ${latency_ms} ms"
else
    echo
    echo "WARN: could not find FAILURE_DETECTED/RECOVERY_COMPLETE timestamps in output" >&2
fi

if [[ "$ft_rc" -ne 0 ]]; then
    echo "FT demo checks: FAIL"
    exit 1
fi
echo "FT demo checks: PASS"

# ------------------------------------------------------------- stage 6

header "STAGE 6/6: sigma/pi calibration -- unit-matched predicted-vs-measured error"
$RUN ./tests/fixtures/topcuoglu_calibrated

header "DEMO COMPLETE"
