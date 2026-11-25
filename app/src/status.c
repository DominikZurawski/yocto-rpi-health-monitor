#include "status.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* Zapisuje fragment JSON o niedostępnej metryce wraz z kodem errno. */
static void print_metric_error(FILE *stream, int error)
{
    fprintf(stream, "{\"available\":false,\"io_error\":%d}", error);
}

/* Zapisuje jeden wiersz JSON na otwartym strumieniu. Zwraca EIO,
 * gdy strumień sygnalizuje błąd. Opróżnienie bufora i zamknięcie należą do wywołującego.
 */
int print_status(FILE *stream, const struct sample *sample, struct health health)
{
    fprintf(stream, "{\"schema_version\":1,\"state\":\"%s\",\"sample_uptime_ms\":",
            health_name(health.level));
    if (sample->uptime_error != 0) {
        fputs("null,\"uptime\":", stream);
        print_metric_error(stream, sample->uptime_error);
    } else {
        fprintf(stream, "%" PRIu64 ",\"uptime\":{\"available\":true,\"ms\":%" PRIu64 "}",
                sample->uptime_ms, sample->uptime_ms);
    }

    fputs(",\"cpu_temperature\":", stream);
    if (sample->cpu_error != 0) {
        print_metric_error(stream, sample->cpu_error);
    } else {
        fprintf(stream, "{\"available\":true,\"level\":\"%s\",\"millicelsius\":%d}",
                health_name(health.cpu_level), sample->cpu_millic);
    }

    fputs(",\"memory\":", stream);
    if (sample->memory_error != 0) {
        print_metric_error(stream, sample->memory_error);
    } else {
        fprintf(stream, "{\"available\":true,\"level\":\"%s\",\"total_kib\":%" PRIu64
                ",\"available_kib\":%" PRIu64 ",\"used_percent\":%u}",
                health_name(health.ram_level), sample->memory.total_kib,
                sample->memory.available_kib, memory_used_percent(&sample->memory));
    }
    fputs("}\n", stream);
    return ferror(stream) ? EIO : 0;
}

/* Tworzy plik tymczasowy obok pliku stanu, zapisuje JSON, zamyka plik
 * i podmienia nazwę przez rename(). Przy błędzie zwraca kod errno
 * i próbuje usunąć plik tymczasowy, zachowując poprzedni stan.
 */
int write_status_atomic(const char *path, const struct sample *sample, struct health health)
{
    char temporary[PATH_MAX];
    int length = snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path);
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        return ENAMETOOLONG;
    }

    int fd = mkstemp(temporary);
    if (fd < 0) {
        return errno;
    }

    FILE *file = fdopen(fd, "w");
    int error;
    if (file == NULL) {
        error = errno;
        close(fd);
    } else {
        error = print_status(file, sample, health);
        if (error == 0 && fflush(file) != 0) {
            error = errno;
        }
        if (error == 0 && fchmod(fd, 0644) != 0) {
            error = errno;
        }
        if (fclose(file) != 0 && error == 0) {
            error = errno;
        }
    }

    /* Czytelnik zobaczy poprzedni albo cały nowy plik. */
    if (error == 0 && rename(temporary, path) != 0) {
        error = errno;
    }
    if (error != 0) {
        unlink(temporary);
    }
    return error;
}
