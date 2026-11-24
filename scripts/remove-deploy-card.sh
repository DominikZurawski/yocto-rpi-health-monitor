#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
image="$project_dir/artifacts/core-image-minimal-raspberrypi3-64.rootfs-20261124103219.wic.bz2"
card=/dev/sda
expected_sha=dd7848b0e5441b5257cab5b0eeb3f927afce82773d3a6b65f3a64b4673df7062

# Verify the exact image and check that the compressed stream is readable.
printf '%s  %s\n' "$expected_sha" "$image" | sha256sum --check
bzip2 --test "$image"

# Check the reader's identity and show the disk and its partitions.
if [[ "$(lsblk -dn -o TYPE,TRAN,MODEL "$card" | xargs)" != "disk usb Storage Device" ]]; then
    printf 'Unexpected device at %s. Stopping.\n' "$card" >&2
    exit 1
fi
lsblk -o PATH,SIZE,MODEL,TRAN,TYPE,MOUNTPOINTS "$card"

printf 'Wpisz dokładnie NADPISZ /dev/sda, aby zapisać obraz: '
IFS= read -r confirmation </dev/tty
[[ "$confirmation" == "NADPISZ /dev/sda" ]] || exit 1

# Unmount the old boot partition if Ubuntu mounted it automatically.
if findmnt -rn -S "${card}1" >/dev/null; then
    sudo umount "${card}1"
fi

# Stop if any filesystem on the card is still mounted.
if lsblk -nr -o MOUNTPOINTS "$card" | grep '[^[:space:]]' >/dev/null; then
    printf 'A filesystem on %s is still mounted. Stopping.\n' "$card" >&2
    exit 1
fi

sudo -v
bzip2 -dc -- "$image" | sudo dd of="$card" bs=4M status=progress conv=fsync
sync
printf '\nZapis zakończony. Możesz wyjąć kartę.\n'
