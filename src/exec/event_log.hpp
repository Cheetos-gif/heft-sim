// src/exec/event_log.hpp
//
// Append-only event log shared by every worker thread. CLAUDE.md
// invariant: the event log is append-only and is the single source of
// truth for all metrics; metrics are computed offline from the log, never
// inline. This class only records; it never aggregates.

#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace heftsim {

enum class EventType { READY, START, FINISH, FAILURE_DETECTED, RECOVERY_COMPLETE };

struct Event {
    int task = -1;
    int processor = -1;
    EventType type = EventType::READY;
    double timestamp = 0.0;  // seconds, CLOCK_MONOTONIC-based, relative to a run's t0
};

// One mutex guards the whole backing vector. Timestamps are read by the
// caller *before* the lock is acquired (so lock-queueing time never
// pollutes a task's own measured cost) -- which means, under contention,
// two events can be appended in a different order than their timestamps
// would sort into. That's an accepted, documented tradeoff: consumers
// must sort by `timestamp`, never assume insertion order. See
// src/exec/dispatcher.hpp hazard #5.
class EventLog {
public:
    void append(Event e);

    // Safe to call while workers are still running (it's just another
    // locked read), but Dispatcher::run() only calls it after every
    // thread has been joined, so callers get a final, stable copy.
    std::vector<Event> snapshot() const;

private:
    mutable std::mutex mutex_;
    std::vector<Event> events_;
};

// Writes events to `path` as CSV: task,processor,event,timestamp_sec.
// Sorted by timestamp first (see the ordering caveat above -- vector
// order is not timestamp order). Throws std::runtime_error if the file
// cannot be opened for writing (e.g. the containing directory doesn't
// exist yet -- callers must create it first).
void write_event_log_csv(std::vector<Event> events, const std::string& path);

}  // namespace heftsim
