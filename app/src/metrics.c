#include "metrics.h"
#include "parse.h"

#include <dirent.h>
#include <limits.h>
#include <stdbool.h>

/* Odczytuje dokładnie jeden wiersz do bufora i zamyka plik.
 * Pusty plik, dodatkowy wiersz i błędy I/O dają kod errno.
 */
static int read_single_line(const char *path, char *line, size_t capacity)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return errno;
    }

    int result = read_line(file, line, capacity);
    int error = result < 0 ? -result : result == 0 ? EINVAL : 0;
    if (error == 0 && fgetc(file) != EOF) {
        error = EINVAL;
    }
    if (ferror(file)) {
        error = EIO;
    }
    if (fclose(file) != 0 && error == 0) {
        error = errno;
    }
    return error;
}

/* Buduje ścieżkę pola type lub temp w wybranej strefie i wykrywa obcięcie. */
static int thermal_path(char *path, size_t size, const char *root, const char *zone,
                        const char *entry)
{
    int length = snprintf(path, size, "%s/%s/%s", root, zone, entry);
    return length < 0 || (size_t)length >= size ? ENAMETOOLONG : 0;
}

/* Wyszukuje strefę thermal_zoneN o zadanym typie. Zwraca w millic
 * temperaturę w milistopniach Celsjusza, jeśli odczyt i walidacja się powiodą.
 */
int read_cpu_temperature(const char *thermal_root, const char *zone_type, int *millic)
{
    static const char prefix[] = "thermal_zone";
    DIR *dir = opendir(thermal_root);
    if (dir == NULL) {
        return errno;
    }

    int error = ENOENT;
    while (1) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0) {
                error = errno;
            }
            break;
        }

        if (strncmp(entry->d_name, prefix, sizeof(prefix) - 1) != 0) {
            continue;
        }
        const char *index = entry->d_name + sizeof(prefix) - 1;
        if (*index == '\0' || index[strspn(index, "0123456789")] != '\0') {
            continue;
        }

        char path[PATH_MAX];
        char line[128];
        int result = thermal_path(path, sizeof(path), thermal_root, entry->d_name, "type");
        if (result == 0) {
            result = read_single_line(path, line, sizeof(line));
        }
        if (result != 0) {
            error = result;
            continue;
        }
        if (strcmp(trim(line), zone_type) != 0) {
            continue;
        }

        error = thermal_path(path, sizeof(path), thermal_root, entry->d_name, "temp");
        if (error == 0) {
            error = read_single_line(path, line, sizeof(line));
        }
        if (error == 0) {
            error = parse_integer(trim(line), -40000, 150000, millic);
        }
        break;
    }
    if (closedir(dir) != 0 && error == 0) {
        error = errno;
    }
    return error;
}

/* Czyta jedno pole sekund i przelicza je na milisekundy bez użycia double.
 * Po sukcesie przesuwa kursor na znak za liczbą i zapisuje wynik.
 */
static int parse_seconds(char **cursor, uint64_t *milliseconds)
{
    char *text = *cursor;
    if (!isdigit((unsigned char)*text)) {
        return EINVAL;
    }
    errno = 0;
    char *end;
    uintmax_t seconds = strtoumax(text, &end, 10);
    if (errno == ERANGE || seconds > UINT64_MAX / 1000) {
        return ERANGE;
    }

    unsigned fraction = 0;
    if (*end == '.') {
        ++end;
        unsigned place = 100;
        if (!isdigit((unsigned char)*end)) {
            return EINVAL;
        }
        while (isdigit((unsigned char)*end) && place != 0) {
            fraction += (unsigned)(*end++ - '0') * place;
            place /= 10;
        }
    }
    if (*end != '\0' && !isspace((unsigned char)*end)) {
        return EINVAL;
    }
    if (seconds * 1000 > UINT64_MAX - fraction) {
        return ERANGE;
    }
    *cursor = end;
    *milliseconds = (uint64_t)(seconds * 1000 + fraction);
    return 0;
}

/* Waliduje oba pola pliku /proc/uptime. Zapisuje tylko pierwsze pole,
 * czyli czas od rozruchu systemu, w milisekundach.
 */
int read_uptime(const char *path, uint64_t *milliseconds)
{
    char line[128];
    int error = read_single_line(path, line, sizeof(line));
    if (error != 0) {
        return error;
    }

    char *cursor = skip_space(line);
    uint64_t uptime;
    uint64_t idle;
    error = parse_seconds(&cursor, &uptime);
    if (error == 0) {
        cursor = skip_space(cursor);
        error = parse_seconds(&cursor, &idle);
    }
    if (error == 0 && *skip_space(cursor) != '\0') {
        error = EINVAL;
    }
    if (error == 0) {
        *milliseconds = uptime;
    }
    return error;
}

/* Odczytuje liczbę z jednostką kB używaną przez /proc/meminfo.
 * Limit zapobiega przepełnieniu przy późniejszym mnożeniu przez 100.
 */
static int parse_kib(char *text, uint64_t *value)
{
    text = skip_space(text);
    if (!isdigit((unsigned char)*text)) {
        return EINVAL;
    }
    errno = 0;
    char *end;
    uintmax_t number = strtoumax(text, &end, 10);
    if (errno == ERANGE || number > UINT64_MAX / 100) {
        return ERANGE;
    }
    if (!isspace((unsigned char)*end) || strcmp(trim(end), "kB") != 0) {
        return EINVAL;
    }
    *value = (uint64_t)number;
    return 0;
}

/* Czyta MemTotal i MemAvailable w KiB, odrzuca braki, duplikaty
 * i wartości sprzeczne. Zapisuje kompletny wynik dopiero po sukcesie.
 */
int read_memory(const char *path, struct memory_usage *memory)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return errno;
    }

    struct memory_usage data = {0};
    bool have_total = false;
    bool have_available = false;
    char line[256];
    int result;
    int error = 0;
    while ((result = read_line(file, line, sizeof(line))) > 0) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            error = have_total ? EINVAL : parse_kib(line + 9, &data.total_kib);
            have_total = true;
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            error = have_available ? EINVAL : parse_kib(line + 13, &data.available_kib);
            have_available = true;
        }
        if (error != 0) {
            break;
        }
    }
    if (result < 0) {
        error = -result;
    }
    if (fclose(file) != 0 && error == 0) {
        error = errno;
    }
    if (error == 0 && (!have_total || !have_available || data.total_kib == 0 ||
                       data.available_kib > data.total_kib)) {
        error = EINVAL;
    }
    if (error == 0) {
        *memory = data;
    }
    return error;
}

/* Oblicza całkowity procent użytego RAM, zaokrąglony w dół.
 * Wymaga poprawnych danych zwróconych przez read_memory().
 */
unsigned memory_used_percent(const struct memory_usage *memory)
{
    return (unsigned)((memory->total_kib - memory->available_kib) * 100 / memory->total_kib);
}
