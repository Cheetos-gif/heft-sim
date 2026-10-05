// src/exec/affinity.cpp
#include "affinity.hpp"

#include <pthread.h>
#include <sched.h>

namespace heftsim {

bool pin_current_thread(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

}  // namespace heftsim
