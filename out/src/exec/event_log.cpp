// src/exec/event_log.cpp
#include "event_log.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace heftsim {

namespace {

const char* event_type_name(EventType t) {
    switch (t) {
        case EventType::READY: return "READY";
        case EventType::START: return "START";
        case EventType::FINISH: return "FINISH";
        case EventType::FAILURE_DETECTED: return "FAILURE_DETECTED";
        case EventType::RECOVERY_COMPLETE: return "RECOVERY_COMPLETE";
    }
    return "UNKNOWN";
}

}  // namespace

void EventLog::append(Event e) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(e);
}

std::vector<Event> EventLog::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_;
}

void write_event_log_csv(std::vector<Event> events, const std::string& path) {
    std::sort(events.begin(), events.end(),
              [](const Event& a, const Event& b) { return a.timestamp < b.timestamp; });

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("write_event_log_csv: could not open '" + path + "' for writing");
    }
    out << "task,processor,event,timestamp_sec\n";
    for (const auto& e : events) {
        out << e.task << ',' << e.processor << ',' << event_type_name(e.type) << ',' << e.timestamp << '\n';
    }
}

}  // namespace heftsim
