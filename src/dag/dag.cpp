// src/dag/dag.cpp
#include "dag.hpp"

#include <algorithm>
#include <stdexcept>

namespace heftsim {

void Dag::add_task(Task task) {
    if (task.id != static_cast<int>(tasks_.size())) {
        throw std::invalid_argument(
            "Dag::add_task: task.id must equal insertion order (expected " +
            std::to_string(tasks_.size()) + ", got " + std::to_string(task.id) + ")");
    }
    for (int pred : task.preds) {
        if (pred < 0 || pred >= static_cast<int>(tasks_.size())) {
            throw std::invalid_argument("Dag::add_task: predecessor id " +
                                         std::to_string(pred) + " not yet added");
        }
    }
    if (tasks_.empty()) {
        num_processors_ = task.cost.size();
    } else if (task.cost.size() != num_processors_) {
        throw std::invalid_argument("Dag::add_task: inconsistent processor count");
    }
    tasks_.push_back(std::move(task));
    succs_.emplace_back();
}

void Dag::add_edge(int from, int to, double comm_cost) {
    if (from < 0 || from >= static_cast<int>(tasks_.size()) || to < 0 ||
        to >= static_cast<int>(tasks_.size())) {
        throw std::invalid_argument("Dag::add_edge: id out of range");
    }
    const auto& to_preds = tasks_[to].preds;
    if (std::find(to_preds.begin(), to_preds.end(), from) == to_preds.end()) {
        throw std::invalid_argument("Dag::add_edge: task " + std::to_string(to) +
                                     " does not list " + std::to_string(from) + " as a predecessor");
    }
    succs_[from].push_back(to);
    edge_cost_[{from, to}] = comm_cost;
}

const Task& Dag::task(int id) const {
    if (id < 0 || id >= static_cast<int>(tasks_.size())) {
        throw std::out_of_range("Dag::task: id out of range");
    }
    return tasks_[id];
}

const std::vector<int>& Dag::successors(int id) const {
    if (id < 0 || id >= static_cast<int>(succs_.size())) {
        throw std::out_of_range("Dag::successors: id out of range");
    }
    return succs_[id];
}

double Dag::comm_cost(int from, int to) const {
    auto it = edge_cost_.find({from, to});
    return it == edge_cost_.end() ? 0.0 : it->second;
}

std::vector<int> Dag::entry_tasks() const {
    std::vector<int> out;
    for (const auto& t : tasks_) {
        if (t.preds.empty()) out.push_back(t.id);
    }
    return out;
}

std::vector<int> Dag::exit_tasks() const {
    std::vector<int> out;
    for (std::size_t i = 0; i < succs_.size(); ++i) {
        if (succs_[i].empty()) out.push_back(static_cast<int>(i));
    }
    return out;
}

}  // namespace heftsim
