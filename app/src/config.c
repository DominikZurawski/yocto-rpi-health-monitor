#include "config.h"
#include "parse.h"

#include <stdbool.h>

/* Wczytuje pary klucz=wartość, pozostawiając domyślne wartości dla pominiętych kluczy.
 * Po sukcesie zapisuje całą konfigurację. Przy błędzie zwraca kod errno i opis w message.
 */
int load_config(const char *path, struct sentinel_config *config, char *message, size_t size)
{
    struct sentinel_config parsed = {
        .interval_sec = 5,
        .cpu_c = {75, 85, 2},
        .ram_percent = {80, 95, 3},
    };
    struct {
        const char *name;
        int *target;
        int min;
        int max;
        bool seen;
    } keys[] = {
        {"interval_sec", &parsed.interval_sec, 1, 10, false},
        {"cpu_warning_c", &parsed.cpu_c.warning, 1, 120, false},
        {"cpu_error_c", &parsed.cpu_c.error, 1, 120, false},
        {"cpu_hysteresis_c", &parsed.cpu_c.hysteresis, 0, 20, false},
        {"ram_warning_percent", &parsed.ram_percent.warning, 1, 100, false},
        {"ram_error_percent", &parsed.ram_percent.error, 1, 100, false},
        {"ram_hysteresis_percent", &parsed.ram_percent.hysteresis, 0, 20, false},
    };

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        int error = errno;
        snprintf(message, size, "%s: %s", path, strerror(error));
        return error;
    }

    char line[256];
    size_t line_number = 0;
    int result;
    int error = 0;
    const char *reason = NULL;
    while ((result = read_line(file, line, sizeof(line))) > 0) {
        ++line_number;
        char *comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
        }
        char *text = trim(line);
        if (*text == '\0') {
            continue;
        }

        char *equals = strchr(text, '=');
        if (equals == NULL) {
            reason = "oczekiwano klucz=wartość";
            error = EINVAL;
            break;
        }
        *equals = '\0';
        char *name = trim(text);
        size_t index = 0;
        const size_t key_count = sizeof(keys) / sizeof(keys[0]);
        while (index < key_count && strcmp(name, keys[index].name) != 0) {
            ++index;
        }

        if (index == key_count || keys[index].seen) {
            error = EINVAL;
            reason = index == key_count ? "nieznany klucz" : "powtórzony klucz";
            break;
        }
        error = parse_integer(equals + 1, keys[index].min, keys[index].max,
                              keys[index].target);
        if (error != 0) {
            reason = "błędna wartość lub przekroczony zakres";
            break;
        }
        keys[index].seen = true;
    }
    if (result < 0) {
        error = -result;
        reason = "nie można odczytać poprawnego wiersza";
        ++line_number;
    }
    if (fclose(file) != 0 && error == 0) {
        error = errno;
        reason = "nie można zamknąć pliku";
    }
    if (error != 0) {
        snprintf(message, size, "%s:%zu: %s", path, line_number, reason);
        return error;
    }
    if (parsed.cpu_c.warning >= parsed.cpu_c.error ||
        parsed.ram_percent.warning >= parsed.ram_percent.error ||
        parsed.cpu_c.hysteresis >= parsed.cpu_c.warning ||
        parsed.ram_percent.hysteresis >= parsed.ram_percent.warning) {
        snprintf(message, size, "%s: wymagane WARNING < ERROR i histereza < WARNING", path);
        return EINVAL;
    }

    *config = parsed;
    return 0;
}
