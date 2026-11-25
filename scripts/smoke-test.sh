#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 || ! "$2" =~ ^[A-Za-z0-9][A-Za-z0-9.:%_-]*$ ]]; then
    printf 'Użycie: bash %s {live|alarm|threshold} HOST [KLUCZ_PRYWATNY]\n' "$0" >&2
    exit 2
fi

mode="$1"
host="$2"
identity="${3:-$HOME/.ssh/id_ed25519}"
ssh_command=(ssh -T -o BatchMode=yes -o ConnectTimeout=10 -i "$identity" "developer@$host")

case "$mode" in
    live)
        first_and_second="$("${ssh_command[@]}" sh -s <<'REMOTE'
set -eu

# Przerywa test i zapisuje przyczynę na stderr, poza próbkami JSON.
fail() {
    printf 'FAIL — %s\n' "$1" >&2
    exit 1
}

unit=rpi-sentinel.service
status=/run/rpi-sentinel/status
test -x /usr/bin/rpi-sentinel || fail 'brak wykonywalnej binarki'
test -r /etc/rpi-sentinel.conf || fail 'brak czytelnej konfiguracji'
systemctl cat "$unit" >/dev/null || fail 'brak jednostki systemd'
systemctl is-enabled --quiet "$unit" || fail 'usługa nie jest włączona przy starcie'
systemctl is-active --quiet "$unit" || fail 'usługa nie działa'
printf 'PASS — binarka, konfiguracja i jednostka obecne, usługa enabled i active.\n' >&2

# Interwał konfiguracji wynosi najwyżej 10 s. Czekamy do 12 s na kolejną próbkę.
first=$(cat "$status") || fail 'brak czytelnego pliku stanu'
attempt=0
second="$first"
while [ "$second" = "$first" ] && [ "$attempt" -lt 12 ]; do
    sleep 1
    second=$(cat "$status") || fail 'nie można ponownie odczytać pliku stanu'
    attempt=$((attempt + 1))
done
[ "$first" != "$second" ] || fail 'plik stanu nie zmienił się przez 12 s'

if ! journal=$(journalctl -b -u "$unit" --no-pager -o cat -n 50); then
    fail 'nie można odczytać dziennika usługi'
fi
grep -q '^{"schema_version":1,' <<EOF || fail 'w bieżącym dzienniku nie ma próbki'
$journal
EOF
printf 'PASS — bieżący dziennik usługi zawiera próbki.\n' >&2
printf '%s\n%s\n' "$first" "$second"
REMOTE
        )"
        # Walidacja JSON odbywa się na laptopie. Obraz Pi nie potrzebuje Pythona.
        python3 - "$first_and_second" <<'PYTHON'
import json
import sys


def read_sample(line):
    """Sprawdza format próbki, dostępność i jednostki wszystkich metryk."""
    sample = json.loads(line)
    cpu = sample["cpu_temperature"]
    memory = sample["memory"]
    uptime = sample["uptime"]
    if sample["schema_version"] != 1 or sample["state"] not in (
        "NORMAL", "WARNING", "ERROR"
    ):
        raise ValueError("nieznana wersja formatu lub stan")
    if not all(metric["available"] is True for metric in (cpu, memory, uptime)):
        raise ValueError("co najmniej jedna metryka jest niedostępna")
    temperature = cpu["millicelsius"]
    total = memory["total_kib"]
    available = memory["available_kib"]
    used = memory["used_percent"]
    milliseconds = uptime["ms"]
    if not all(type(value) is int for value in (temperature, total, available, used, milliseconds)):
        raise ValueError("metryka nie jest liczbą całkowitą")
    if not (-40000 <= temperature <= 150000 and total > 0 and
            0 <= available <= total and 0 <= used <= 100 and
            milliseconds > 0 and sample["sample_uptime_ms"] == milliseconds):
        raise ValueError("metryka ma niespójne wartości")
    return milliseconds


try:
    samples = sys.argv[1].splitlines()
    previous, current = map(read_sample, samples)
    if current <= previous:
        raise ValueError("czas pracy w drugiej próbce nie wzrósł")
except (KeyError, TypeError, ValueError) as error:
    print(f"FAIL — pomiary lub aktualizacja pliku stanu: {error}", file=sys.stderr)
    sys.exit(1)

print("PASS — odczytano CPU, RAM i uptime, a plik stanu zawiera nowszą próbkę.")
print(samples[-1])
PYTHON
        ;;
    alarm|threshold)
        "${ssh_command[@]}" sh -s -- "$mode" <<'REMOTE'
set -eu

workspace=$(mktemp -d)
monitor_pid=

# Zamyka testową instancję i usuwa jej prywatny katalog.
cleanup() {
    if [ -n "$monitor_pid" ]; then
        kill -TERM "$monitor_pid" 2>/dev/null || :
        wait "$monitor_pid" 2>/dev/null || :
    fi
    rm -rf -- "$workspace"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

# Czeka na próbkę ze wskazanym stanem i temperaturą.
await_sample() {
    expected_state="$1"
    expected_temperature="$2"
    attempt=0
    while [ "$attempt" -lt 8 ]; do
        sample=$(cat "$workspace/status" 2>/dev/null) || sample=
        case "$sample" in
            *\"state\":\""$expected_state"\"*,*\"millicelsius\":"$expected_temperature"\}*)
                printf 'PASS — stan %s przy %s m°C.\n' "$expected_state" "$expected_temperature"
                return 0
                ;;
        esac
        kill -0 "$monitor_pid" 2>/dev/null || break
        sleep 1
        attempt=$((attempt + 1))
    done
    printf 'FAIL — oczekiwano %s przy %s m°C.\n' "$expected_state" "$expected_temperature" >&2
    printf 'Ostatnia próbka: %s\n' "$sample" >&2
    cat "$workspace/events" >&2
    return 1
}

# Podmienia fikcyjny odczyt w taki sposób, żeby monitor widział cały wiersz.
set_temperature() {
    printf '%s\n' "$1" > "$workspace/temperature"
    mv "$workspace/temperature" "$workspace/thermal/thermal_zone0/temp"
}

mkdir -p "$workspace/proc" "$workspace/thermal/thermal_zone0"
printf 'cpu-thermal\n' > "$workspace/thermal/thermal_zone0/type"
printf '123.45 0.00\n' > "$workspace/proc/uptime"
printf 'MemTotal: 1000 kB\nMemAvailable: 600 kB\n' > "$workspace/proc/meminfo"
# Ustawia próg ostrzegawczy dla kolejnego uruchomienia instancji testowej.
write_config() {
    cat > "$workspace/config" <<CONFIG
interval_sec=1
cpu_warning_c=$1
cpu_error_c=85
cpu_hysteresis_c=2
CONFIG
}

# Usuwa poprzednią próbkę, aby kolejny odczyt pochodził z nowego procesu.
start_monitor() {
    rm -f -- "$workspace/status"
    /usr/bin/rpi-sentinel --config "$workspace/config" --status-file "$workspace/status" \
        --proc-root "$workspace/proc" --thermal-root "$workspace/thermal" \
        > /dev/null 2> "$workspace/events" &
    monitor_pid=$!
}

# Wysyła SIGTERM i wymaga zakończenia procesu z kodem 0.
stop_monitor() {
    kill -TERM "$monitor_pid"
    if ! wait "$monitor_pid"; then
        monitor_pid=
        printf 'FAIL — testowa instancja nie zakończyła się poprawnie.\n' >&2
        return 1
    fi
    monitor_pid=
}

if [ "$1" = threshold ]; then
    set_temperature 70000
    write_config 75
    start_monitor
    await_sample NORMAL 70000
    stop_monitor
    write_config 65
    start_monitor
    await_sample WARNING 70000
    stop_monitor
    printf 'PASS — zmiana progu z 75°C na 65°C przy stałej temperaturze 70°C wywołała WARNING.\n'
    exit 0
fi

write_config 75
set_temperature 60000
start_monitor
await_sample NORMAL 60000
set_temperature 76000
await_sample WARNING 76000
set_temperature 72999
await_sample NORMAL 72999

stop_monitor
if ! grep -F 'Zmiana stanu: NORMAL -> WARNING' "$workspace/events" ||
   ! grep -F 'Zmiana stanu: WARNING -> NORMAL' "$workspace/events"; then
    printf 'FAIL — brakuje zapisu przejścia stanu.\n' >&2
    exit 1
fi
REMOTE
        ;;
    *)
        printf 'Nieznany tryb: %s. Wybierz live, alarm lub threshold.\n' "$mode" >&2
        exit 2
        ;;
esac
