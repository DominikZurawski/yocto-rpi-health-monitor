#ifndef SENTINEL_METRICS_H
#define SENTINEL_METRICS_H

#include <stdint.h>

struct memory_usage {
    uint64_t total_kib;
    uint64_t available_kib;
};

struct sample {
    int cpu_error;
    int uptime_error;
    int memory_error;
    int cpu_millic;
    uint64_t uptime_ms;
    struct memory_usage memory;
};

/* Wynikiem jest 0 albo kod errno. Wartość wyjściowa jest ważna przy sukcesie. */
/* Czyta pole temp wybranej strefy termicznej w milistopniach Celsjusza. */
int read_cpu_temperature(const char *thermal_root, const char *zone_type, int *millic);
/* Czyta czas od rozruchu i zwraca go w milisekundach. */
int read_uptime(const char *path, uint64_t *milliseconds);
/* Zwraca całkowitą i dostępną pamięć w KiB. */
int read_memory(const char *path, struct memory_usage *memory);
/* Zwraca procent w zakresie 0–100 dla zwalidowanej próbki pamięci. */
unsigned memory_used_percent(const struct memory_usage *memory);

#endif
