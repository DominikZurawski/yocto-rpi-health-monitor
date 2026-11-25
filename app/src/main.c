#include "config.h"
#include "health.h"
#include "metrics.h"
#include "status.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <systemd/sd-daemon.h>
#include <time.h>

#ifndef SENTINEL_CONFIG_PATH
#define SENTINEL_CONFIG_PATH "/etc/rpi-sentinel.conf"
#endif
#define DEFAULT_STATUS_DIRECTORY "/run/rpi-sentinel"
#define DEFAULT_STATUS_PATH DEFAULT_STATUS_DIRECTORY "/status"

static volatile sig_atomic_t stop_requested;

/* Obsługuje SIGTERM przez ustawienie flagi sig_atomic_t. Zapis pliku
 * i logowanie zostają w zwykłym przebiegu programu.
 */
static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

/* Wypisuje dostępne argumenty na wskazanym strumieniu. */
static void print_usage(FILE *stream, const char *program)
{
    fprintf(stream, "Użycie: %s [--config FILE] [--status-file FILE] [--proc-root DIR]\n"
            "    [--thermal-root DIR] [--thermal-type TYPE]\n", program);
}

/* Blokuje SIGTERM podczas pomiarów i rejestruje procedurę obsługi.
 * Zapisuje maskę dla pselect(), które odblokowuje sygnał przy oczekiwaniu.
 */
static int setup_shutdown(sigset_t *wait_mask)
{
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &blocked, wait_mask) != 0) {
        return errno;
    }
    sigdelset(wait_mask, SIGTERM);

    struct sigaction action = {.sa_handler = request_stop};
    sigemptyset(&action.sa_mask);
    return sigaction(SIGTERM, &action, NULL) == 0 ? 0 : errno;
}

/* Przelicza termin w CLOCK_MONOTONIC na pozostały czas i czeka przez pselect().
 * Po sygnale wraca do pętli, która sprawdza flagę zakończenia.
 */
static int wait_until(struct timespec deadline, const sigset_t *wait_mask)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return errno;
    }
    struct timespec remaining = {
        .tv_sec = deadline.tv_sec - now.tv_sec,
        .tv_nsec = deadline.tv_nsec - now.tv_nsec,
    };
    if (remaining.tv_nsec < 0) {
        --remaining.tv_sec;
        remaining.tv_nsec += 1000000000L;
    }
    if (remaining.tv_sec < 0) {
        remaining = (struct timespec){0};
    }

    if (pselect(0, NULL, NULL, NULL, &remaining, wait_mask) < 0 && errno != EINTR) {
        return errno;
    }
    return 0;
}

/* Loguje nowy błąd lub powrót odczytu. Taki sam błąd w następnym
 * cyklu nie powoduje powtórzenia komunikatu.
 */
static void log_metric_error(const char *metric, int error, int previous_error)
{
    if (error != 0 && error != previous_error) {
        fprintf(stderr, "Błąd odczytu %s: %s\n", metric, strerror(error));
    } else if (error == 0 && previous_error != 0) {
        fprintf(stderr, "Odczyt %s znów dostępny.\n", metric);
    }
}

/* Zapewnia istnienie domyślnego katalogu w /run i publikuje JSON.
 * Przy własnej ścieżce katalog nadrzędny musi już istnieć.
 */
static int publish_status(const char *path, const struct sample *sample, struct health health)
{
    if (strcmp(path, DEFAULT_STATUS_PATH) == 0 &&
        mkdir(DEFAULT_STATUS_DIRECTORY, 0755) != 0 && errno != EEXIST) {
        return errno;
    }
    return write_status_atomic(path, sample, health);
}

/* Wczytuje argumenty i konfigurację, a następnie w każdym cyklu
 * mierzy, ocenia, loguje i publikuje stan do odebrania SIGTERM.
 */
int main(int argc, char **argv)
{
    const char *config_path = SENTINEL_CONFIG_PATH;
    const char *status_path = DEFAULT_STATUS_PATH;
    const char *proc_root = "/proc";
    const char *thermal_root = "/sys/class/thermal";
    const char *thermal_type = "cpu-thermal";
    const struct option options[] = {
        {"config", required_argument, NULL, 'c'},
        {"status-file", required_argument, NULL, 'o'},
        {"proc-root", required_argument, NULL, 'p'},
        {"thermal-root", required_argument, NULL, 's'},
        {"thermal-type", required_argument, NULL, 't'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int option;
    opterr = 0;
    while ((option = getopt_long(argc, argv, "c:o:p:s:t:h", options, NULL)) != -1) {
        if (optarg != NULL && *optarg == '\0') {
            print_usage(stderr, argv[0]);
            return EXIT_FAILURE;
        }
        switch (option) {
        case 'c': config_path = optarg; break;
        case 'o': status_path = optarg; break;
        case 'p': proc_root = optarg; break;
        case 's': thermal_root = optarg; break;
        case 't': thermal_type = optarg; break;
        case 'h': print_usage(stdout, argv[0]); return EXIT_SUCCESS;
        default: print_usage(stderr, argv[0]); return EXIT_FAILURE;
        }
    }
    if (optind != argc) {
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    char uptime_path[PATH_MAX];
    char memory_path[PATH_MAX];
    int uptime_length = snprintf(uptime_path, sizeof(uptime_path), "%s/uptime", proc_root);
    int memory_length = snprintf(memory_path, sizeof(memory_path), "%s/meminfo", proc_root);
    if (uptime_length < 0 || (size_t)uptime_length >= sizeof(uptime_path) ||
        memory_length < 0 || (size_t)memory_length >= sizeof(memory_path) ||
        strlen(thermal_type) >= 128) {
        fprintf(stderr, "Za długa ścieżka wejścia lub nazwa strefy.\n");
        return EXIT_FAILURE;
    }

    struct sentinel_config config;
    char message[256];
    if (load_config(config_path, &config, message, sizeof(message)) != 0) {
        fprintf(stderr, "Konfiguracja: %s\n", message);
        return EXIT_FAILURE;
    }

    uint64_t watchdog_usec;
    int watchdog = sd_watchdog_enabled(1, &watchdog_usec);
    if (watchdog < 0) {
        fprintf(stderr, "Konfiguracja watchdoga systemd: %s\n", strerror(-watchdog));
        return EXIT_FAILURE;
    }
    if (watchdog > 0 && (uint64_t)config.interval_sec * 1000000 > watchdog_usec / 2) {
        fprintf(stderr, "Interwał pomiarów przekracza połowę limitu watchdoga systemd.\n");
        return EXIT_FAILURE;
    }

    sigset_t wait_mask;
    int error = setup_shutdown(&wait_mask);
    if (error != 0) {
        fprintf(stderr, "Obsługa SIGTERM: %s\n", strerror(error));
        return EXIT_FAILURE;
    }

    struct sample previous_sample = {0};
    struct health previous_health = {0};
    bool first_sample = true;
    int previous_status_error = 0;
    while (!stop_requested) {
        struct timespec deadline;
        if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
            perror("Odczyt zegara monotonicznego");
            return EXIT_FAILURE;
        }
        deadline.tv_sec += config.interval_sec;

        struct sample sample = {0};
        sample.cpu_error = read_cpu_temperature(thermal_root, thermal_type, &sample.cpu_millic);
        sample.uptime_error = read_uptime(uptime_path, &sample.uptime_ms);
        sample.memory_error = read_memory(memory_path, &sample.memory);
        log_metric_error("temperatury CPU", sample.cpu_error, previous_sample.cpu_error);
        log_metric_error("uptime", sample.uptime_error, previous_sample.uptime_error);
        log_metric_error("RAM", sample.memory_error, previous_sample.memory_error);

        struct health health = evaluate_health(&sample, &config, previous_health);
        if (first_sample) {
            fprintf(stderr, "Stan początkowy: %s\n", health_name(health.level));
        } else if (health.level != previous_health.level) {
            fprintf(stderr, "Zmiana stanu: %s -> %s\n",
                    health_name(previous_health.level), health_name(health.level));
        }
        if (print_status(stdout, &sample, health) != 0 || fflush(stdout) != 0) {
            fprintf(stderr, "Nie można zapisać dziennika pomiarów.\n");
            return EXIT_FAILURE;
        }

        int status_error = publish_status(status_path, &sample, health);
        if (status_error != 0 && status_error != previous_status_error) {
            fprintf(stderr, "Zapis stanu do %s: %s\n", status_path, strerror(status_error));
        } else if (status_error == 0 && previous_status_error != 0) {
            fprintf(stderr, "Zapis stanu znów dostępny.\n");
        }

        /* Powiadomienie potwierdza zakończenie cyklu. Obsłużony błąd
         * odczytu albo zapisu zostaje zgłoszony w stanie lub dzienniku.
         */
        if (first_sample || watchdog > 0) {
            const char *notification = first_sample
                ? (watchdog > 0 ? "READY=1\nWATCHDOG=1" : "READY=1")
                : "WATCHDOG=1";
            int notified = sd_notify(0, notification);
            if (notified < 0 || (watchdog > 0 && notified == 0)) {
                fprintf(stderr, "Powiadomienie systemd: %s\n",
                        notified < 0 ? strerror(-notified) : "brak gniazda powiadomień");
                return EXIT_FAILURE;
            }
        }

        first_sample = false;
        previous_sample = sample;
        previous_health = health;
        previous_status_error = status_error;

        error = wait_until(deadline, &wait_mask);
        if (error != 0) {
            fprintf(stderr, "Oczekiwanie na pomiar: %s\n", strerror(error));
            return EXIT_FAILURE;
        }
    }

    fprintf(stderr, "Odebrano SIGTERM. Zakończono pomiary.\n");
    return EXIT_SUCCESS;
}
