// src/exec/affinity.cpp
#include "affinity.hpp"

#include <pthread.h>
#include <sched.h>
#include <thread>

namespace heftsim {

bool pin_current_thread(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

int core_for_processor(int processor) {
    unsigned hc = std::thread::hardware_concurrency();
    if (hc == 0) hc = 1;
    return static_cast<int>(static_cast<unsigned>(processor) % hc);
}

}  // namespace heftsim
