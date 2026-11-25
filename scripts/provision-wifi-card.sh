#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode=interactive
if [[ "${1:-}" == --saved ]]; then
    mode=saved
    shift
fi

if [[ $# -ne 2 || ! -b "$1" || ! -f "$2" || "$2" != *.pub ]]; then
    printf 'Użycie: %s [--saved] PARTYCJA_ROOT KLUCZ.pub\n' "$0" >&2
    exit 2
fi

partition="$(realpath -- "$1")"
public_key="$(realpath -- "$2")"
if [[ "$(lsblk -dn -o TYPE "$partition")" != part ]] ||
   [[ -n "$(lsblk -dn -o MOUNTPOINTS "$partition")" ]]; then
    printf 'Wskaż odmontowaną partycję root karty microSD.\n' >&2
    exit 1
fi
if [[ "$mode" == interactive ]]; then
    ssh-keygen -lf "$public_key"
    lsblk -o PATH,SIZE,TYPE,FSTYPE,MOUNTPOINTS "/dev/$(lsblk -dn -o PKNAME "$partition")"
    printf 'Wpisz ścieżkę partycji, by potwierdzić konfigurację: '
    IFS= read -r confirmation
    [[ "$confirmation" == "$partition" ]] || exit 1
else
    ssh-keygen -lf "$public_key" >/dev/null
fi

work_dir="$(mktemp -d)"
mount_dir="$work_dir/card"
mkdir "$mount_dir"
cleanup() {
    if mountpoint -q "$mount_dir"; then
        sudo umount "$mount_dir" || return
    fi
    rm -f -- "$work_dir/wifi.conf"
    rmdir -- "$mount_dir" "$work_dir"
}
trap cleanup EXIT

sudo mount -- "$partition" "$mount_dir"
if ! sudo test -f "$mount_dir/etc/rpi-health-monitor/stage2-wifi-image"; then
    printf 'Brak znacznika obrazu Wi-Fi na partycji %s.\n' "$partition" >&2
    exit 1
fi

# Zapisz SSID szesnastkowo i wyprowadź klucz WPA2 w pamięci procesu.
umask 077
python3 - "$project_dir/.local/wifi-default" "$mode" > "$work_dir/wifi.conf" <<'PY'
import getpass
import hashlib
import stat
import sys
from pathlib import Path

def read_ssid(terminal, prompt):
    print(prompt, end="", flush=True, file=sys.stderr)
    line = terminal.readline()
    if not line:
        raise SystemExit("Przerwano odczyt nazwy sieci.")
    return line.removesuffix("\n").encode("utf-8")


def derive_psk(ssid, password):
    if not 1 <= len(ssid) <= 32:
        raise SystemExit("SSID musi mieć od 1 do 32 bajtów.")
    if not 8 <= len(password) <= 63:
        raise SystemExit("Hasło WPA2 musi mieć od 8 do 63 bajtów.")
    return hashlib.pbkdf2_hmac("sha1", password, ssid, 4096, 32)


def read_psk(ssid, prompt):
    return derive_psk(ssid, getpass.getpass(prompt).encode("utf-8"))


def read_saved_network(path):
    with path.open("rb") as config:
        if stat.S_IMODE(path.stat().st_mode) != 0o600:
            raise SystemExit(f"Ustaw uprawnienia pliku przez chmod 600: {path}")
        lines = config.read().split(b"\n")
    if lines[-1] == b"":
        lines.pop()
    if len(lines) != 2:
        raise SystemExit(f"Podaj SSID w pierwszym wierszu i hasło w drugim: {path}")
    ssid, password = lines
    return ssid, derive_psk(ssid, password)


saved_network = Path(sys.argv[1])
if sys.argv[2] == "saved":
    if not saved_network.is_file():
        raise SystemExit(f"Brak zapisanego profilu Wi-Fi: {saved_network}")
    default_ssid, default_psk = read_saved_network(saved_network)
    networks = [(default_ssid, default_psk, 20)]
else:
    with open("/dev/tty", encoding="utf-8") as terminal:
        if saved_network.is_file():
            default_ssid, default_psk = read_saved_network(saved_network)
        else:
            default_ssid = read_ssid(terminal, "Nazwa sieci domyślnej (SSID): ")
            default_psk = read_psk(default_ssid, "Hasło sieci domyślnej: ")
        networks = [(default_ssid, default_psk, 20)]
        fallback_ssid = read_ssid(terminal, "Nazwa sieci zapasowej (Enter pomija): ")
        if fallback_ssid:
            if fallback_ssid == default_ssid:
                raise SystemExit("Sieć zapasowa musi mieć inną nazwę niż sieć domyślna.")
            networks.append((fallback_ssid, read_psk(fallback_ssid, "Hasło sieci zapasowej: "), 10))

print("country=PL")
for ssid, psk, priority in networks:
    print("network={")
    print(f"    ssid={ssid.hex()}")
    print("    key_mgmt=WPA-PSK")
    print(f"    psk={psk.hex()}")
    print(f"    priority={priority}")
    print("}")
PY

sudo install -o root -g root -m 0600 "$work_dir/wifi.conf" "$mount_dir/etc/rpi-health-monitor/wifi.conf"
# OpenSSH odczytuje klucz publiczny z uprawnieniami użytkownika developer.
sudo install -o root -g root -m 0644 "$public_key" "$mount_dir/etc/ssh/authorized_keys/developer"
sudo umount "$mount_dir"
printf 'Karta jest gotowa do uruchomienia Raspberry Pi.\n'
