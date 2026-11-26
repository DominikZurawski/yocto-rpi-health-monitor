#ifndef SENTINEL_METRICS_H
#define SENTINEL_METRICS_H

#include <stdint.h>

struct sentinel_memory_usage {
    uint64_t total_kib;
    uint64_t available_kib;
};

/* Funkcje odczytu zwracają 0 albo dodatni kod errno.
 * Przekaż poprawne ścieżki i wskaźniki. Wartość wyjściowa jest ważna przy sukcesie.
 */
/* Czyta pole temp wybranej strefy termicznej w milistopniach Celsjusza. */
int sentinel_read_cpu_temperature(const char *thermal_root, const char *zone_type, int *millic);
/* Czyta czas od rozruchu i zwraca go w milisekundach. */
int sentinel_read_uptime(const char *path, uint64_t *milliseconds);
/* Zwraca całkowitą i dostępną pamięć w KiB. */
int sentinel_read_memory(const char *path, struct sentinel_memory_usage *memory);
/* Zwraca procent w zakresie 0–100 dla zwalidowanej próbki pamięci. */
unsigned sentinel_memory_used_percent(const struct sentinel_memory_usage *memory);

#endif
