#include "config.h"
#include "health.h"
#include <sentinel/metrics.h>
#include "status.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: niespełniono %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

/* Tworzy plik testowy z podaną treścią i sprawdza zapis oraz zamknięcie. */
static void write_fixture(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    CHECK(fputs(text, file) >= 0);
    CHECK(fclose(file) == 0);
}

/* Sprawdza poprawne i błędne odczyty trzech metryk na plikach zastępczych. */
static void test_metrics(const char *directory, const char *path)
{
    uint64_t uptime;
    CHECK(sentinel_read_uptime(path, &uptime) == ENOENT);
    CHECK(sentinel_read_memory(path, &(struct sentinel_memory_usage){0}) == ENOENT);
    CHECK(sentinel_read_cpu_temperature(path, "cpu-thermal", &(int){0}) == ENOENT);
    write_fixture(path, "0.000 0.00\n");
    CHECK(sentinel_read_uptime(path, &uptime) == 0 && uptime == 0);
    write_fixture(path, "123.45 678.90\n");
    CHECK(sentinel_read_uptime(path, &uptime) == 0 && uptime == 123450);
    write_fixture(path, "123.45 -1.00\n");
    CHECK(sentinel_read_uptime(path, &uptime) == EINVAL);
    write_fixture(path, "18446744073709551.616 0.00\n");
    CHECK(sentinel_read_uptime(path, &uptime) == ERANGE);
    write_fixture(path, "1.0 2.0 extra\n");
    CHECK(sentinel_read_uptime(path, &uptime) == EINVAL);
    write_fixture(path, "1.25\n");
    CHECK(sentinel_read_uptime(path, &uptime) == EINVAL);
    write_fixture(path, "1.2345 2.0\n");
    CHECK(sentinel_read_uptime(path, &uptime) == EINVAL);
    write_fixture(path, "1.25 2.0\n3.0 4.0\n");
    CHECK(sentinel_read_uptime(path, &uptime) == EINVAL);

    struct sentinel_memory_usage memory;
    write_fixture(path, "MemTotal: 1000 kB\nMemFree: 100 kB\nMemAvailable: 250 kB\n");
    CHECK(sentinel_read_memory(path, &memory) == 0 && sentinel_memory_used_percent(&memory) == 75);
    write_fixture(path, "MemTotal: 1000 kB\nMemFree: 100 kB\n");
    CHECK(sentinel_read_memory(path, &memory) == EINVAL);
    write_fixture(path, "MemTotal: 1 kB\nMemAvailable: 2 kB\n");
    CHECK(sentinel_read_memory(path, &memory) == EINVAL);
    write_fixture(path, "MemTotal: 1 MB\nMemAvailable: 1 kB\n");
    CHECK(sentinel_read_memory(path, &memory) == EINVAL);
    write_fixture(path, "MemTotal: 18446744073709551615 kB\nMemAvailable: 1 kB\n");
    CHECK(sentinel_read_memory(path, &memory) == ERANGE);
    write_fixture(path, "MemTotal: 0 kB\nMemAvailable: 0 kB\n");
    CHECK(sentinel_read_memory(path, &memory) == EINVAL);
    write_fixture(path, "MemTotal: 1 kB\nMemAvailable: 1 kB\nMemAvailable: 0 kB\n");
    CHECK(sentinel_read_memory(path, &memory) == EINVAL);
    write_fixture(path, "MemAvailable: 0 kB\nMemTotal: 1 kB\n");
    CHECK(sentinel_read_memory(path, &memory) == 0 && sentinel_memory_used_percent(&memory) == 100);
    write_fixture(path, "MemTotal: 3 kB\nMemAvailable: 3 kB\n");
    CHECK(sentinel_read_memory(path, &memory) == 0 && sentinel_memory_used_percent(&memory) == 0);
    write_fixture(path, "MemTotal: 3 kB\nMemAvailable: 2 kB\n");
    CHECK(sentinel_read_memory(path, &memory) == 0 && sentinel_memory_used_percent(&memory) == 33);

    char zone[128];
    char type[160];
    char temperature_path[160];
    int temperature;
    snprintf(zone, sizeof(zone), "%s/thermal_zone7", directory);
    snprintf(type, sizeof(type), "%s/type", zone);
    snprintf(temperature_path, sizeof(temperature_path), "%s/temp", zone);
    CHECK(mkdir(zone, 0700) == 0);
    write_fixture(type, "cpu-thermal\n");
    CHECK(sentinel_read_cpu_temperature(directory, "cpu-thermal", &temperature) == ENOENT);
    write_fixture(temperature_path, "75125\n");
    CHECK(sentinel_read_cpu_temperature(directory, "gpu-thermal", &temperature) == ENOENT);
    CHECK(sentinel_read_cpu_temperature(directory, "cpu-thermal", &temperature) == 0 && temperature == 75125);
    write_fixture(temperature_path, "-2500\n");
    CHECK(sentinel_read_cpu_temperature(directory, "cpu-thermal", &temperature) == 0 && temperature == -2500);
    write_fixture(temperature_path, "999999\n");
    CHECK(sentinel_read_cpu_temperature(directory, "cpu-thermal", &temperature) == ERANGE);
    write_fixture(temperature_path, "75 C\n");
    CHECK(sentinel_read_cpu_temperature(directory, "cpu-thermal", &temperature) == EINVAL);
    write_fixture(temperature_path, "");
    CHECK(sentinel_read_cpu_temperature(directory, "cpu-thermal", &temperature) == EINVAL);
    write_fixture(temperature_path, "-40000\n");
    CHECK(sentinel_read_cpu_temperature(directory, "cpu-thermal", &temperature) == 0 && temperature == -40000);
    write_fixture(temperature_path, "-40001\n");
    CHECK(sentinel_read_cpu_temperature(directory, "cpu-thermal", &temperature) == ERANGE);
    write_fixture(temperature_path, "150000\n");
    CHECK(sentinel_read_cpu_temperature(directory, "cpu-thermal", &temperature) == 0 && temperature == 150000);
    write_fixture(temperature_path, "150001\n");
    CHECK(sentinel_read_cpu_temperature(directory, "cpu-thermal", &temperature) == ERANGE);
    CHECK(unlink(temperature_path) == 0 && unlink(type) == 0 && rmdir(zone) == 0);
}

/* Sprawdza domyślne wartości, komentarze, błędne klucze i granice konfiguracji. */
static void test_config(const char *path)
{
    char message[256];
    struct sentinel_config config;
    CHECK(load_config(path, &config, message, sizeof(message)) == ENOENT);
    write_fixture(path, "# Pominięte klucze zachowują wartości domyślne.\n");
    CHECK(load_config(path, &config, message, sizeof(message)) == 0);
    CHECK(config.interval_sec == 5 && config.cpu_c.warning == 75 &&
          config.ram_percent.error == 95);
    write_fixture(path, "# Przykład\n interval_sec = 1 # komentarz\ncpu_warning_c=76");
    CHECK(load_config(path, &config, message, sizeof(message)) == 0);
    CHECK(config.interval_sec == 1 && config.cpu_c.warning == 76 && config.cpu_c.error == 85);
    write_fixture(path, "\ninvalid=3\n");
    CHECK(load_config(path, &config, message, sizeof(message)) == EINVAL);
    CHECK(strstr(message, ":2:") != NULL);
    write_fixture(path, "interval_sec=1\ninterval_sec=2\n");
    CHECK(load_config(path, &config, message, sizeof(message)) == EINVAL);
    write_fixture(path, "cpu_warning_c=90\n");
    CHECK(load_config(path, &config, message, sizeof(message)) == EINVAL);
    write_fixture(path, "interval_sec=10\n");
    CHECK(load_config(path, &config, message, sizeof(message)) == 0 && config.interval_sec == 10);
    const char *invalid_intervals[] = {"0", "-1", "11", "123456789012345678901234567890"};
    for (size_t index = 0; index < sizeof(invalid_intervals) / sizeof(invalid_intervals[0]); ++index) {
        char fixture[128];
        snprintf(fixture, sizeof(fixture), "interval_sec=%s\n", invalid_intervals[index]);
        write_fixture(path, fixture);
        CHECK(load_config(path, &config, message, sizeof(message)) == ERANGE);
        CHECK(config.interval_sec == 10);
    }
    write_fixture(path, "interval_sec= # pominięta wartość\n");
    CHECK(load_config(path, &config, message, sizeof(message)) == EINVAL);
    write_fixture(path, "interval_sec\n");
    CHECK(load_config(path, &config, message, sizeof(message)) == EINVAL);
    write_fixture(path, "ram_warning_percent=95\nram_error_percent=95\n");
    CHECK(load_config(path, &config, message, sizeof(message)) == EINVAL);
    char overlong_line[300];
    memset(overlong_line, 'a', sizeof(overlong_line) - 1);
    overlong_line[sizeof(overlong_line) - 1] = '\0';
    write_fixture(path, overlong_line);
    CHECK(load_config(path, &config, message, sizeof(message)) == EOVERFLOW);
    FILE *binary = fopen(path, "w");
    CHECK(binary != NULL);
    static const char embedded_null[] = "interval_sec=1\0hidden\n";
    CHECK(fwrite(embedded_null, 1, sizeof(embedded_null) - 1, binary) == sizeof(embedded_null) - 1);
    CHECK(fclose(binary) == 0);
    CHECK(load_config(path, &config, message, sizeof(message)) == EINVAL);
}

/* Sprawdza dokładne progi, histerezę i powrót po błędzie odczytu. */
static void test_health(void)
{
    struct sentinel_config config = {.interval_sec = 1, .cpu_c = {75, 85, 2},
                                     .ram_percent = {80, 95, 3}};
    struct sample sample = {.cpu_millic = 74999, .memory = {1000, 500}};
    struct health state = evaluate_health(&sample, &config, (struct health){0});
    CHECK(state.level == HEALTH_NORMAL);
    sample.cpu_millic = 75000;
    state = evaluate_health(&sample, &config, state);
    CHECK(state.level == HEALTH_WARNING);
    sample.cpu_millic = 85000;
    state = evaluate_health(&sample, &config, state);
    CHECK(state.level == HEALTH_ERROR);
    sample.cpu_millic = 83000;
    CHECK(evaluate_health(&sample, &config, state).level == HEALTH_ERROR);
    sample.cpu_millic = 82999;
    state = evaluate_health(&sample, &config, state);
    CHECK(state.level == HEALTH_WARNING);
    sample.cpu_millic = 73000;
    CHECK(evaluate_health(&sample, &config, state).level == HEALTH_WARNING);
    sample.cpu_millic = 72999;
    state = evaluate_health(&sample, &config, state);
    CHECK(state.level == HEALTH_NORMAL);
    sample.memory.available_kib = 200;
    state = evaluate_health(&sample, &config, state);
    CHECK(state.level == HEALTH_WARNING);
    sample.memory.available_kib = 50;
    state = evaluate_health(&sample, &config, state);
    CHECK(state.level == HEALTH_ERROR);
    sample.memory.available_kib = 80;
    state = evaluate_health(&sample, &config, state);
    CHECK(state.level == HEALTH_ERROR);
    sample.memory.available_kib = 90;
    state = evaluate_health(&sample, &config, state);
    CHECK(state.level == HEALTH_WARNING);
    sample.memory.available_kib = 240;
    sample.uptime_error = EIO;
    state = evaluate_health(&sample, &config, state);
    CHECK(state.level == HEALTH_ERROR && state.ram_level == HEALTH_NORMAL);
    sample.uptime_error = 0;
    CHECK(evaluate_health(&sample, &config, state).level == HEALTH_NORMAL);
}

/* Sprawdza powrót do NORMAL przy spadku temperatury o 1 m°C poniżej progu. */
static void test_zero_cpu_hysteresis(const char *path)
{
    struct sentinel_config config;
    char message[256];
    write_fixture(path, "cpu_warning_c=75\ncpu_hysteresis_c=0\n");
    CHECK(load_config(path, &config, message, sizeof(message)) == 0);
    CHECK(config.cpu_c.hysteresis == 0);

    struct sample sample = {.cpu_millic = 75000, .memory = {1000, 500}};
    struct health state = evaluate_health(&sample, &config, (struct health){0});
    CHECK(state.cpu_level == HEALTH_WARNING && state.level == HEALTH_WARNING);

    sample.cpu_millic = 74999;
    state = evaluate_health(&sample, &config, state);
    CHECK(state.cpu_level == HEALTH_NORMAL && state.level == HEALTH_NORMAL);
}

/* Sprawdza odczyt starej i nowej wersji po podmianie oraz sprzątanie
 * pliku tymczasowego po nieudanym rename().
 */
static void test_status(const char *directory, const char *path)
{
    struct sample sample = {.cpu_millic = 82000, .uptime_ms = 100, .memory = {1000, 300}};
    struct health health = {.level = HEALTH_WARNING, .cpu_level = HEALTH_WARNING};
    write_fixture(path, "poprzedni stan\n");
    FILE *old = fopen(path, "r");
    CHECK(old != NULL);
    CHECK(write_status_atomic(path, &sample, health) == 0);
    char line[1024];
    CHECK(fgets(line, sizeof(line), old) != NULL && strcmp(line, "poprzedni stan\n") == 0);
    CHECK(fclose(old) == 0);

    FILE *current = fopen(path, "r");
    CHECK(current != NULL);
    const char *expected =
        "{\"schema_version\":1,\"state\":\"WARNING\",\"sample_uptime_ms\":100,"
        "\"uptime\":{\"available\":true,\"ms\":100},"
        "\"cpu_temperature\":{\"available\":true,\"level\":\"WARNING\",\"millicelsius\":82000},"
        "\"memory\":{\"available\":true,\"level\":\"NORMAL\",\"total_kib\":1000,"
        "\"available_kib\":300,\"used_percent\":70}}\n";
    CHECK(fgets(line, sizeof(line), current) != NULL && strcmp(line, expected) == 0);
    CHECK(fgetc(current) == EOF && !ferror(current));
    CHECK(fclose(current) == 0);

    /* Wymuś błąd po rozpoczęciu zapisu bez zmiany uprawnień ani zajmowania dysku. */
    struct rlimit original_limit;
    CHECK(getrlimit(RLIMIT_FSIZE, &original_limit) == 0);
    struct rlimit short_limit = original_limit;
    short_limit.rlim_cur = 16;
    struct sigaction original_action;
    struct sigaction ignore = {.sa_handler = SIG_IGN};
    CHECK(sigemptyset(&ignore.sa_mask) == 0);
    CHECK(sigaction(SIGXFSZ, &ignore, &original_action) == 0);
    CHECK(setrlimit(RLIMIT_FSIZE, &short_limit) == 0);
    sample.uptime_ms = 200;
    int write_error = write_status_atomic(path, &sample, health);
    CHECK(setrlimit(RLIMIT_FSIZE, &original_limit) == 0);
    CHECK(sigaction(SIGXFSZ, &original_action, NULL) == 0);
    CHECK(write_error == EFBIG);
    current = fopen(path, "r");
    CHECK(current != NULL);
    CHECK(fgets(line, sizeof(line), current) != NULL && strcmp(line, expected) == 0);
    CHECK(fgetc(current) == EOF && !ferror(current));
    CHECK(fclose(current) == 0);

    char occupied[128];
    snprintf(occupied, sizeof(occupied), "%s/occupied", directory);
    CHECK(mkdir(occupied, 0700) == 0);
    CHECK(write_status_atomic(occupied, &sample, health) != 0);
    CHECK(rmdir(occupied) == 0);
}

/* Uruchamia testy jednostkowe z własnym katalogiem tymczasowym. */
int main(void)
{
    char directory[] = "/tmp/rpi-sentinel-unit.XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[128];
    snprintf(path, sizeof(path), "%s/input", directory);
    test_metrics(directory, path);
    CHECK(unlink(path) == 0);
    test_config(path);
    test_health();
    test_zero_cpu_hysteresis(path);
    test_status(directory, path);
    CHECK(unlink(path) == 0 && rmdir(directory) == 0);
    return EXIT_SUCCESS;
}
