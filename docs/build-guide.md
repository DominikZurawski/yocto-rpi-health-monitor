# Stage 2 build guide

The Git repository at `/mnt/windows1/Wspolne/Dokumenty/Projekty/Yocto/rpi-sentinel`
contains project documentation, source pins, and configuration. The native
ext4 workspace at `$HOME/yocto/rpi-sentinel-wrynose` holds upstream checkouts
under `layers/`. The separate `build-stage2/` directory will hold generated
configuration, logs, and images. The earlier `build/` directory remains
available for its existing QEMU configuration.

## Host inspection

The host reported Ubuntu 26.04.1 LTS, Linux 7.0.0-44-generic on x86-64,
a Ryzen 7 260 with 8 cores and 16 threads, 14 GiB visible RAM, 4 GiB
swap, and about 415 GiB free on ext4. The documented host packages and
`en_US.UTF-8` were present. Commands used for inspection:

```bash
cat /etc/os-release
uname -srmo
lscpu
free -h
swapon --show
findmnt -T "$HOME" -no FSTYPE
df -h "$HOME" .
locale -a
dpkg-query -W git gcc gawk chrpath diffstat texinfo
git -C "$HOME/yocto/rpi-sentinel-wrynose/layers/meta-raspberrypi" rev-parse HEAD
```

## Prepare and build

Run from the project repository:

```bash
bash scripts/build-stage2.sh
```

The script clones missing repositories and checks existing revisions against
[sources.lock](../build-config/sources.lock). It initializes `build-stage2`,
copies the project configuration, adds the Poky and Raspberry Pi layers,
parses the metadata, and builds `core-image-minimal`. It stops on errors.
Each run replaces the build configuration copy and the corresponding logs
under `build-stage2/logs/`. Set `YOCTO_DIR` to use another absolute workspace
path. Existing upstream checkouts must be clean and match the lock file.

The expected deploy directory is
`$HOME/yocto/rpi-sentinel-wrynose/build-stage2/tmp/deploy/images/raspberrypi3-64/`.
Check the actual path with `bitbake-getvar`. After a successful build, the
script copies the timestamped SD image into the project's `artifacts/`
directory. Git ignores that directory. The flash script reads the verified
image listed below from `artifacts/`; to flash a later build, update its
image filename and verified checksum together.

## Verification status

The developer ran the build script on 2026-11-24. The build log reports
4,963 attempted tasks, all successful, and 36 warnings. Some source
downloads failed at the primary URL and BitBake tried mirrors. The
package manifest confirms systemd 259.5.

- SD image: `artifacts/core-image-minimal-raspberrypi3-64.rootfs-20261124103219.wic.bz2`
- SHA-256 of the compressed image: `dd7848b0e5441b5257cab5b0eeb3f927afce82773d3a6b65f3a64b4673df7062`
- Build log: `build-stage2/logs/build.log` in the ext4 workspace.

The developer flashed the image and confirmed that Raspberry Pi 3 reached
the local login prompt. Network and developer SSH checks are pending.
