#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    printf 'Użycie: %s {enable|read} ZAMONTOWANA_PARTYCJA_ROOT\n' "$0" >&2
    exit 2
fi

case "$1" in
    enable|wlacz) mode=enable ;;
    read|odczytaj) mode=read ;;
    *)
        printf 'Użycie: %s {enable|read} ZAMONTOWANA_PARTYCJA_ROOT\n' "$0" >&2
        exit 2
        ;;
esac
partition="$(realpath -- "$2")"
if [[ ! -b "$partition" || "$(lsblk -dn -o TYPE,FSTYPE,LABEL "$partition" | xargs)" != "part ext4 root" ]]; then
    printf 'Wskaż partycję ext4 z etykietą root na karcie.\n' >&2
    exit 1
fi
root_dir="$(findmnt -rn -S "$partition" -o TARGET)" || {
    printf 'Najpierw zamontuj kartę: udisksctl mount -b %s\n' "$partition" >&2
    exit 1
}
if [[ -z "$root_dir" || "$root_dir" == / || "$root_dir" == *$'\n'* ]] ||
    ! sudo test -f "$root_dir/etc/rpi-health-monitor/stage2-wifi-image"; then
    printf 'Nie znaleziono prawidłowo zamontowanego obrazu deweloperskiego.\n' >&2
    exit 1
fi

case "$mode" in
    enable)
        parent_disk="$(lsblk -dn -o PKNAME "$partition")"
        if [[ -z "$parent_disk" ]]; then
            printf 'Nie można ustalić urządzenia karty dla %s.\n' "$partition" >&2
            exit 1
        fi
        boot_partition="$(lsblk -nrpo PATH,TYPE,FSTYPE,LABEL "/dev/$parent_disk" |
            awk '$2 == "part" && $3 == "vfat" && $4 == "boot" { print $1 }')"
        # Domyślnie /var/log wskazuje na ulotny katalog w RAM.
        if [[ -L "$root_dir/var/log" ]]; then
            test "$(readlink "$root_dir/var/log")" = volatile/log
            test ! -e "$root_dir/var/log.stage2-backup"
            test ! -L "$root_dir/var/log.stage2-backup"
            sudo mv -- "$root_dir/var/log" "$root_dir/var/log.stage2-backup"
        fi
        wifi_dropin_dir="$root_dir/etc/systemd/system/stage2-wifi@wlan0.service.d"
        sudo mkdir -p "$root_dir/var/log/journal" "$root_dir/etc/systemd/journald.conf.d" \
            "$wifi_dropin_dir"
        # Krótszy interwał ogranicza utratę logów przy odłączeniu zasilania.
        printf '[Journal]\nStorage=persistent\nSystemMaxUse=16M\nSyncIntervalSec=5s\n' |
            sudo tee "$root_dir/etc/systemd/journald.conf.d/90-stage2-diagnostics.conf" >/dev/null
        # Włącz diagnostykę negocjacji WPA2 bez opcji -K, która ujawnia klucze.
        sudo tee "$wifi_dropin_dir/90-stage2-diagnostics.conf" >/dev/null <<'EOF'
[Service]
ExecStart=
ExecStart=/usr/sbin/wpa_supplicant -d -i %I -c /etc/rpi-health-monitor/wifi.conf
EOF
        udisksctl unmount -b "$partition"
        if [[ -n "$boot_partition" && "$boot_partition" != *$'\n'* ]] &&
           findmnt -rn -S "$boot_partition" >/dev/null; then
            udisksctl unmount -b "$boot_partition"
        fi
        printf 'Zapis dziennika włączony. Możesz przełożyć kartę do Raspberry Pi.\n'
        ;;
    read)
        if [[ -L "$root_dir/var/log" ]] ||
           ! sudo test -d "$root_dir/var/log/journal"; then
            printf 'Na tej karcie nie ma trwałego dziennika. Uruchom: bash %s enable %s\n' \
                "$0" "$partition" >&2
            exit 1
        fi
        boots="$(sudo journalctl --root="$root_dir" --list-boots --no-pager)"
        boot_id="$(awk '$1 == 0 { print $2 }' <<< "$boots")"
        if [[ ! "$boot_id" =~ ^[0-9a-f]{32}$ ]]; then
            printf 'Brak zapisanego rozruchu na karcie.\n' >&2
            exit 1
        fi
        printf 'Ostatni rozruch: %s\n' "$boot_id"

        sudo journalctl --root="$root_dir" --boot="$boot_id" --no-pager -n 18 \
            -u stage2-wifi@wlan0.service -u systemd-networkd.service -u sshd.socket \
            --grep='Trying to associate|Associated with|Authentication|CTRL-EVENT-(CONNECTED|DISCONNECTED|SSID-TEMP-DISABLED)|RX EAPOL|EAPOL-Key|4-Way Handshake|Key negotiation completed|DHCP.*wlan0|wlan0:.*(DHCP|carrier)|Listening on sshd|Dependency failed'
        sudo journalctl --root="$root_dir" --boot="$boot_id" --no-pager -n 8 -k \
            --grep='brcmfmac.*(failed|Firmware:)|regulatory\.db|renamed from wlan'
        ;;
esac
