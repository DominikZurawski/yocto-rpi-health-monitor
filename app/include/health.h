#ifndef SENTINEL_HEALTH_H
#define SENTINEL_HEALTH_H

#include "config.h"
#include "sample.h"

enum health_level {
    HEALTH_NORMAL,
    HEALTH_WARNING,
    HEALTH_ERROR,
};

struct health {
    enum health_level level;
    enum health_level cpu_level;
    enum health_level ram_level;
};

/* Zwraca stan próbki, uwzględniając poprzednie poziomy CPU i RAM. */
struct health evaluate_health(const struct sample *sample, const struct sentinel_config *config,
                              struct health previous);
/* Zwraca stały tekst. Wywołujący nie musi go zwalniać. */
const char *health_name(enum health_level level);

#endif
