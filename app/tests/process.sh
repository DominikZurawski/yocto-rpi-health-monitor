#!/bin/sh
set -eu

program="$1"
scenario="${2:-transitions}"
workspace="$(mktemp -d)"
sentinel_pid=
# Zatrzymuje testowany proces, jeśli jeszcze działa, i usuwa pliki testowe.
cleanup() {
    if [ -n "$sentinel_pid" ]; then
        kill -TERM "$sentinel_pid" 2>/dev/null || :
        wait "$sentinel_pid" 2>/dev/null || :
    fi
    rm -rf -- "$workspace"
}
trap cleanup EXIT

# Czeka na oczekiwany stan z limitem czasu i sprawdza, czy proces nadal działa.
await_status() {
    pattern="$1"
    attempts=0
    until grep -q "$pattern" "$workspace/status" 2>/dev/null; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 40 ] || ! kill -0 "$sentinel_pid" 2>/dev/null; then
            printf 'Nie osiągnięto oczekiwanego stanu (%s).\n' "$pattern" >&2
            cat "$workspace/errors" >&2
            return 1
        fi
        sleep 0.1
    done
}

# Uruchamia monitor z plikami testowymi i zapisuje PID do oczekiwania i sprzątania.
start_monitor() {
    "$program" --config "$workspace/config" --status-file "$workspace/status" \
        --proc-root "$workspace/proc" --thermal-root "$workspace/sys" \
        > "$workspace/measurements" 2> "$workspace/errors" &
    sentinel_pid=$!
}

# Wysyła SIGTERM i sprawdza, czy proces obsłużył sygnał i zwrócił kod 0.
stop_monitor() {
    kill -TERM "$sentinel_pid"
    wait "$sentinel_pid"
    sentinel_pid=
    grep -q 'Odebrano SIGTERM' "$workspace/errors"
}

mkdir -p "$workspace/proc" "$workspace/sys/thermal_zone3"
printf 'cpu-thermal\n' > "$workspace/sys/thermal_zone3/type"
printf '60000\n' > "$workspace/sys/thermal_zone3/temp"
printf '1.25 0.05\n' > "$workspace/proc/uptime"
printf 'MemTotal: 1000 kB\nMemAvailable: 500 kB\n' > "$workspace/proc/meminfo"

if [ "$scenario" = shutdown ]; then
    printf 'interval_sec=10\n' > "$workspace/config"
    start_monitor
    await_status '"state":"NORMAL"'
    stop_monitor
    test "$(wc -l < "$workspace/measurements")" -eq 1
    printf 'OK — SIGTERM zakończył proces przed następnym pomiarem przy interwale 10 s.\n'
    exit 0
fi

printf 'interval_sec=1\n' > "$workspace/config"
printf 'interval_sec=0\n' > "$workspace/invalid"

if "$program" --config "$workspace/invalid" --status-file "$workspace/status" \
    > "$workspace/invalid.stdout" 2> "$workspace/invalid.stderr"; then
    echo 'Program przyjął niepoprawną konfigurację.' >&2
    exit 1
fi
test ! -e "$workspace/status"
grep -q 'invalid:1:' "$workspace/invalid.stderr"
printf 'OK — błędna konfiguracja odrzucona przed zapisem stanu.\n'

start_monitor
await_status '"state":"NORMAL"'
printf 'OK — pierwszy stan: NORMAL.\n'

# Zmiana nazwy daje procesowi cały nowy plik pomiaru.
printf '76000\n' > "$workspace/temp.new"
mv "$workspace/temp.new" "$workspace/sys/thermal_zone3/temp"
await_status '"state":"WARNING"'
printf 'OK — temperatura 76°C: NORMAL → WARNING.\n'

mv "$workspace/proc/meminfo" "$workspace/meminfo.saved"
await_status '"state":"ERROR"'
grep -q '"memory":{"available":false,' "$workspace/status"
grep -q '"millicelsius":76000' "$workspace/status"
printf 'OK — brak meminfo: WARNING → ERROR; odczyt temperatury nadal dostępny.\n'

mv "$workspace/meminfo.saved" "$workspace/proc/meminfo"
await_status '"state":"WARNING"'
printf 'OK — przywrócenie meminfo: ERROR → WARNING.\n'

printf '72999\n' > "$workspace/temp.new"
mv "$workspace/temp.new" "$workspace/sys/thermal_zone3/temp"
await_status '"state":"NORMAL"'
grep -q '"millicelsius":72999' "$workspace/status"
stop_monitor

grep -q 'Zmiana stanu: NORMAL -> WARNING' "$workspace/errors"
grep -q 'Zmiana stanu: WARNING -> ERROR' "$workspace/errors"
grep -q 'Zmiana stanu: ERROR -> WARNING' "$workspace/errors"
grep -q 'Zmiana stanu: WARNING -> NORMAL' "$workspace/errors"
grep -q 'Odczyt RAM znów dostępny' "$workspace/errors"
test "$(wc -l < "$workspace/measurements")" -ge 5
printf 'OK — spadek temperatury poniżej granicy histerezy: WARNING → NORMAL.\n'
printf 'OK — SIGTERM zakończył proces kodem 0.\n'
