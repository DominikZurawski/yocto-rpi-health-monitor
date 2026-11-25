#ifndef SENTINEL_CONFIG_H
#define SENTINEL_CONFIG_H

#include <stddef.h>

struct thresholds {
    int warning;
    int error;
    int hysteresis;
};

struct sentinel_config {
    int interval_sec;
    struct thresholds cpu_c;
    struct thresholds ram_percent;
};

/* Zapisuje config przy sukcesie. Przy błędzie zwraca kod errno i wypełnia message. */
int load_config(const char *path, struct sentinel_config *config, char *message, size_t size);

#endif
