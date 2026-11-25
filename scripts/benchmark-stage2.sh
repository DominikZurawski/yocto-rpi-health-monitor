#!/usr/bin/env bash
set -eo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
yocto_dir="${YOCTO_DIR:-$HOME/yocto/rpi-sentinel-wrynose}"
baseline_dir="$yocto_dir/build-stage2"
report_dir="${1:-$project_dir/artifacts/benchmark-$(date +%Y%m%d-%H%M%S)}"
targets=(virtual/kernel systemd busybox wpa-supplicant openssh curl zstd)
trials=(8:2 8:4 12:2 12:4)

if [[ $# -gt 1 ]]; then
    trials=("${@:2}")
fi
declare -A seen_trials
for trial in "${trials[@]}"; do
    if [[ ! "$trial" =~ ^[1-9][0-9]*:[1-9][0-9]*$ ]]; then
        printf 'Nieprawidłowa próba %s. Podaj liczbę zadań i prac w formacie 8:6.\n' "$trial" >&2
        exit 2
    fi
    if [[ -n "${seen_trials[$trial]:-}" ]]; then
        printf 'Próba %s występuje więcej niż raz.\n' "$trial" >&2
        exit 2
    fi
    seen_trials[$trial]=1
done

if [[ ! -f "$baseline_dir/conf/bblayers.conf" || ! -d "$baseline_dir/downloads" ||
      ! -d "$baseline_dir/sstate-cache" ]]; then
    printf 'Brakuje konfiguracji, pobranych źródeł lub cache obrazu bazowego.\n' >&2
    exit 1
fi
while IFS='|' read -r name url revision layer_paths; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    name="${name% }"
    revision="${revision// /}"
    checkout="$yocto_dir/layers/$name"
    if [[ "$(git -C "$checkout" rev-parse HEAD)" != "$revision" ||
          -n "$(git -C "$checkout" status --porcelain)" ]]; then
        printf 'Sprawdź przypiętą rewizję i stan repozytorium %s.\n' "$checkout" >&2
        exit 1
    fi
done < "$project_dir/build-config/sources.lock"

mkdir -p "$report_dir"
report_dir="$(realpath -- "$report_dir")"
workspace="$(mktemp -d "$yocto_dir/benchmark-stage2.XXXXXXXX")"
# Każda próba zna te same sygnatury wyników z lokalnego mirrora.
python3 - "$baseline_dir/cache/hashserv.db" "$workspace/hashserv.db" <<'PY'
import sqlite3
import sys
from pathlib import Path

source = Path(sys.argv[1]).as_uri()
with sqlite3.connect(source + "?mode=ro", uri=True) as original:
    with sqlite3.connect(sys.argv[2]) as snapshot:
        original.backup(snapshot)
PY
printf '%s\n' "$workspace" > "$report_dir/workspace.txt"
printf '%s\n' "${targets[@]}" > "$report_dir/targets.txt"
printf 'bitbake_threads\tmake_jobs\tstatus\telapsed_s\tmin_available_kib\tswap_in_pages\tswap_out_pages\n' > "$report_dir/results.tsv"

sample_memory() {
    while :; do
        awk '/^MemAvailable:/ { print $2 }' /proc/meminfo
        sleep 5
    done
}

read_swap() {
    awk '$1 == "pswpin" { input = $2 } $1 == "pswpout" { output = $2 } END { print input, output }' /proc/vmstat
}

run_trial() (
    threads="$1"
    jobs="$2"
    label="${threads}-j${jobs}"
    build_dir="$workspace/$label"
    source "$yocto_dir/layers/openembedded-core/oe-init-build-env" "$build_dir"
    mkdir -p cache
    cp "$workspace/hashserv.db" cache/hashserv.db
    cp "$baseline_dir/conf/bblayers.conf" conf/bblayers.conf
    cp "$project_dir/build-config/stage2.conf" conf/stage2.conf
    cat >> conf/local.conf <<CONFIG

require conf/stage2.conf
BB_NUMBER_THREADS = "$threads"
PARALLEL_MAKE = "-j$jobs"
DL_DIR = "$baseline_dir/downloads"
SSTATE_MIRRORS = "file://.* file://$baseline_dir/sstate-cache/PATH"
BB_HASHSERVE_UPSTREAM = ""
BB_HASHSERVE_DB_DIR = "$build_dir/cache"
BB_NO_NETWORK = "1"
BB_DISKMON_DIRS = "HALT,\${TMPDIR},20G,100K"
CONFIG
    printf 'Start próby %s. Log: %s/%s.log\n' "$label" "$report_dir" "$label"

    read -r swap_in_before swap_out_before < <(read_swap)
    sample_memory > "$report_dir/$label.memory-kib" &
    sampler_pid=$!
    trap 'kill "$sampler_pid" 2>/dev/null || :; wait "$sampler_pid" 2>/dev/null || :' EXIT

    status=0
    /usr/bin/time -f '%e' -o "$report_dir/$label.time" \
        bitbake -c compile "${targets[@]}" > "$report_dir/$label.log" 2>&1 || status=$?

    kill "$sampler_pid" 2>/dev/null || :
    wait "$sampler_pid" 2>/dev/null || :
    trap - EXIT
    read -r swap_in_after swap_out_after < <(read_swap)
    elapsed="$(tail -n 1 "$report_dir/$label.time")"
    min_memory="$(awk 'NR == 1 || $1 < minimum { minimum = $1 } END { print minimum }' "$report_dir/$label.memory-kib")"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$threads" "$jobs" "$status" "$elapsed" "$min_memory" \
        "$((swap_in_after - swap_in_before))" "$((swap_out_after - swap_out_before))" >> "$report_dir/results.tsv"
    printf 'Koniec próby %s: kod %s, czas %s s.\n' "$label" "$status" "$elapsed"
)

for trial in "${trials[@]}"; do
    run_trial "${trial%:*}" "${trial#*:}"
done
cat "$report_dir/results.tsv"
awk -F '\t' 'NR > 1 && $3 != 0 { failed = 1 } END { exit failed }' "$report_dir/results.tsv"
