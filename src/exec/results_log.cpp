// src/exec/results_log.cpp
#include "results_log.hpp"

#include <fstream>
#include <stdexcept>

namespace heftsim {

void append_result_row(const std::string& path, const ResultRow& row) {
    bool exists = std::ifstream(path).good();

    std::ofstream out(path, std::ios::app);
    if (!out) {
        throw std::runtime_error("append_result_row: failed to open " + path + " for writing");
    }

    if (!exists) {
        out << "dag_id,num_tasks,num_processors,scheduler,predicted_sec,measured_sec,error_pct,throughput\n";
    }

    out << row.dag_id << ',' << row.num_tasks << ',' << row.num_processors << ',' << row.scheduler << ','
        << row.predicted_sec << ',' << row.measured_sec << ',' << row.error_pct << ',' << row.throughput << '\n';
}

}  // namespace heftsim
