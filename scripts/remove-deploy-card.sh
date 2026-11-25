#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 || $# -gt 4 || ! "$3" =~ ^[[:xdigit:]]{64}$ ]]; then
    printf 'Użycie: %s URZĄDZENIE_KARTY OBRAZ SHA256 [KLUCZ.pub]\n' "$0" >&2
    exit 2
fi

card="$(realpath -- "$1")"
image="$2"
expected_sha="$3"
public_key="${4:-}"
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -eq 4 ]]; then
    profile="$project_dir/.local/wifi-default"
    if [[ ! -f "$profile" || "$(stat -Lc %a -- "$profile")" != 600 ]]; then
        printf 'Automatyczna konfiguracja wymaga pliku %s z trybem 0600.\n' "$profile" >&2
        exit 1
    fi
    if [[ "$public_key" != *.pub || ! -f "$public_key" ]] ||
       ! ssh-keygen -lf "$public_key" >/dev/null; then
        printf 'Podaj prawidłowy klucz publiczny SSH w pliku .pub.\n' >&2
        exit 1
    fi
fi

check_card() {
    local description mountpoints
    if [[ ! -b "$card" ]] ||
       ! description="$(lsblk -dn -o TYPE,TRAN,MODEL "$card")" ||
       [[ "$(xargs <<< "$description")" != "disk usb Storage Device" ]]; then
        printf 'Urządzenie %s nie odpowiada używanemu czytnikowi kart USB.\n' "$card" >&2
        return 1
    fi
    mountpoints="$(lsblk -nr -o MOUNTPOINTS "$card")" || return 1
    if grep -Eq '^(/|/boot(/.*)?|/home|/usr|/var|\[SWAP\])$' <<< "$mountpoints"; then
        printf 'Urządzenie %s zawiera aktywną partycję systemową lub swap.\n' "$card" >&2
        return 1
    fi
}

check_card
printf '%s  %s\n' "$expected_sha" "$image" | sha256sum --check
bzip2 --test "$image"
lsblk -o PATH,SIZE,MODEL,TRAN,TYPE,MOUNTPOINTS "$card"

printf 'Wpisz dokładnie NADPISZ %s, aby nadpisać całą kartę: ' "$card"
IFS= read -r confirmation </dev/tty
[[ "$confirmation" == "NADPISZ $card" ]] || exit 1

sudo -v
check_card

# Odmontuj wyłącznie partycje urządzenia potwierdzonego przez użytkownika.
partitions="$(lsblk -nrpo PATH,TYPE "$card")"
while read -r partition device_type; do
    if [[ "$device_type" == part ]] && findmnt -rn -S "$partition" >/dev/null; then
        printf 'Odmontowuję %s.\n' "$partition"
        udisksctl unmount -b "$partition"
    fi
done <<< "$partitions"

# Ubuntu może ponownie zamontować partycję. Zapis wymaga pustej listy montowań.
check_card
mountpoints="$(lsblk -nr -o MOUNTPOINTS "$card")"
if [[ "$mountpoints" =~ [^[:space:]] ]]; then
    printf 'Urządzenie %s jest nadal używane. Przerywam przed zapisem.\n' "$card" >&2
    exit 1
fi

bzip2 -dc -- "$image" | sudo dd of="$card" bs=4M status=progress conv=fsync
printf '\nZapis obrazu zakończony.\n'

if [[ -n "$public_key" ]]; then
    udevadm settle
    root_partition="$(lsblk -nrpo PATH,TYPE,FSTYPE,LABEL "$card" |
        awk '$2 == "part" && $3 == "ext4" && $4 == "root" { print $1 }')"
    if [[ -z "$root_partition" || "$root_partition" == *$'\n'* ]]; then
        printf 'Nie znaleziono jednej partycji ext4 z etykietą root na %s.\n' "$card" >&2
        exit 1
    fi
    if findmnt -rn -S "$root_partition" >/dev/null; then
        udisksctl unmount -b "$root_partition"
    fi
    bash "$project_dir/scripts/provision-wifi-card.sh" --saved "$root_partition" "$public_key"
else
    printf 'Karta jest gotowa do oddzielnej konfiguracji Wi-Fi.\n'
fi
