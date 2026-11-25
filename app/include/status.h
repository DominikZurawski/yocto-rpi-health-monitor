#ifndef SENTINEL_STATUS_H
#define SENTINEL_STATUS_H

#include "health.h"

#include <stdio.h>

/* Jeśli uptime_ms jest dostępne, służy też jako czas próbki od rozruchu. */
int print_status(FILE *stream, const struct sample *sample, struct health health);
/* Atomowo zastępuje plik stanu. Zwraca 0 przy sukcesie lub kod errno. */
int write_status_atomic(const char *path, const struct sample *sample, struct health health);

#endif
