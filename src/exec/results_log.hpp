// src/exec/results_log.hpp
//
// Machine-readable results log for multi-DAG, multi-scheduler benchmark
// sweeps (benchmark_suite.cpp): one row per (dag, scheduler) run,
// appended as it's produced so a crash mid-sweep doesn't lose prior rows
// -- same append-only discipline src/exec/event_log.hpp already uses for
// per-task events, at a coarser granularity. analysis/ (Python) is the
// only consumer; this header only writes, same "metrics computed
// offline, never inline" discipline CLAUDE.md states for the event log.

#pragma once

#include <string>

namespace heftsim {

struct ResultRow {
    std::string dag_id;
    int num_tasks = 0;
    int num_processors = 0;
    std::string scheduler;
    double predicted_sec = 0.0;
    double measured_sec = 0.0;
    double error_pct = 0.0;
    double throughput = 0.0;  // tasks/sec, from measured_sec
};

// Appends one row to `path` as CSV:
//   dag_id,num_tasks,num_processors,scheduler,predicted_sec,measured_sec,error_pct,throughput
// Writes the header first if `path` doesn't exist yet. No locking --
// every runner that writes this file is single-process, single-threaded
// at the point it calls this (unlike src/exec/dispatcher.cpp's EventLog,
// which genuinely needs synchronization against concurrent worker
// threads). Throws std::runtime_error if the file cannot be opened for
// writing.
void append_result_row(const std::string& path, const ResultRow& row);

}  // namespace heftsim
