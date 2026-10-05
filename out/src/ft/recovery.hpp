// src/ft/recovery.hpp
//
// Recovery policy: this feature's whole contract is "recovery re-executes
// it once" -- exactly one retry per task, never more. This constant is the
// single source of truth for that cap; src/exec/dispatcher.cpp's retry()
// is the mechanism, src/ft/heartbeat_monitor.* is the trigger, and the
// EventType::RECOVERY_COMPLETE entry in the event log
// (src/exec/event_log.hpp) is the observable outcome.
//
// A second failure on an already-retried task is out of contract: it is
// logged (a second FAILURE_DETECTED) but not retried again -- see
// Dispatcher::retry()'s idempotency guard.

#pragma once

namespace heftsim {

inline constexpr int kMaxRecoveryAttempts = 1;

}  // namespace heftsim
