#ifndef SENTINEL_SAMPLE_H
#define SENTINEL_SAMPLE_H

#include <sentinel/metrics.h>
#include <stdint.h>

struct sample {
    int cpu_error;
    int uptime_error;
    int memory_error;
    int cpu_millic;
    uint64_t uptime_ms;
    struct sentinel_memory_usage memory;
};

#endif
