#include "health.h"

/* Wyznacza poziom jednej metryki. Przejście w górę zachodzi na progu,
 * a przejście w dół dopiero poniżej progu pomniejszonego o histerezę.
 */
static enum health_level evaluate_metric(int value, struct thresholds limits,
                                        enum health_level previous)
{
    if (value >= limits.error ||
        (previous == HEALTH_ERROR && value >= limits.error - limits.hysteresis)) {
        return HEALTH_ERROR;
    }
    if (value >= limits.warning ||
        (previous >= HEALTH_WARNING && value >= limits.warning - limits.hysteresis)) {
        return HEALTH_WARNING;
    }
    return HEALTH_NORMAL;
}

/* Łączy poziomy CPU i RAM w jeden stan. Błąd dowolnego odczytu
 * daje ERROR, a błędny pomiar CPU lub RAM zachowuje jego poprzedni
 * poziom na potrzeby histerezy po odzyskaniu odczytu.
 */
struct health evaluate_health(const struct sample *sample, const struct sentinel_config *config,
                              struct health previous)
{
    struct health result = previous;

    if (sample->cpu_error == 0) {
        struct thresholds limits = {
            .warning = config->cpu_c.warning * 1000,
            .error = config->cpu_c.error * 1000,
            .hysteresis = config->cpu_c.hysteresis * 1000,
        };
        result.cpu_level = evaluate_metric(sample->cpu_millic, limits, previous.cpu_level);
    }
    if (sample->memory_error == 0) {
        result.ram_level = evaluate_metric((int)sentinel_memory_used_percent(&sample->memory),
                                          config->ram_percent, previous.ram_level);
    }

    if (sample->cpu_error || sample->memory_error || sample->uptime_error) {
        result.level = HEALTH_ERROR;
    } else {
        result.level = result.cpu_level > result.ram_level ? result.cpu_level : result.ram_level;
    }
    return result;
}

/* Zwraca stałą nazwę poziomu używaną w logu i JSON. */
const char *health_name(enum health_level level)
{
    switch (level) {
    case HEALTH_NORMAL:
        return "NORMAL";
    case HEALTH_WARNING:
        return "WARNING";
    case HEALTH_ERROR:
        return "ERROR";
    }
    return "UNKNOWN";
}
