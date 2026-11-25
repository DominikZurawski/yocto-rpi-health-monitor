#!/usr/bin/env bash
set -eo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
yocto_dir="${YOCTO_DIR:-$HOME/yocto/rpi-sentinel-wrynose}"
build_dir="$yocto_dir/build-stage2"
artifact_dir="$project_dir/artifacts"
case "${1:-}" in
    "") ;;
    --wifi-ssh)
        build_dir="$yocto_dir/build-stage2-wifi-ssh"
        artifact_dir="$artifact_dir/wifi-ssh"
        ;;
    *)
        printf 'Usage: %s [--wifi-ssh]\n' "$0" >&2
        exit 2
        ;;
esac

mkdir -p "$yocto_dir/layers"
cd "$yocto_dir"

while IFS='|' read -r name url revision layer; do
    [[ "$name" == \#* || -z "$name" ]] && continue
    name="${name% }"
    url="${url# }"
    url="${url% }"
    revision="${revision# }"
    revision="${revision% }"
    repo="$yocto_dir/layers/$name"
    if [[ ! -e "$repo" ]]; then
        git clone "$url" "$repo"
        git -C "$repo" checkout --detach "$revision"
    fi
    actual="$(git -C "$repo" rev-parse HEAD)"
    if [[ "$actual" != "$revision" || -n "$(git -C "$repo" status --porcelain)" ]]; then
        printf 'Unexpected revision or local changes in %s\nExpected: %s\nActual:   %s\n' \
            "$repo" "$revision" "$actual" >&2
        exit 1
    fi
    printf '%s %s\n' "$name" "$actual"
done < "$project_dir/build-config/sources.lock"

source "$yocto_dir/layers/openembedded-core/oe-init-build-env" "$build_dir"
cp "$project_dir/build-config/stage2.conf" conf/stage2.conf
if ! grep -Fxq 'require conf/stage2.conf' conf/local.conf; then
    printf '\nrequire conf/stage2.conf\n' >> conf/local.conf
fi
if [[ "${1:-}" == --wifi-ssh ]]; then
    mkdir -p classes
    cp "$project_dir/build-config/classes/stage2-wifi-ssh.bbclass" classes/
    cp "$project_dir/build-config/stage2-wifi-ssh.conf" conf/
    if ! grep -Fxq 'require conf/stage2-wifi-ssh.conf' conf/local.conf; then
        printf '\nrequire conf/stage2-wifi-ssh.conf\n' >> conf/local.conf
    fi
fi

for layer in "$yocto_dir/layers/meta-yocto/meta-poky" "$yocto_dir/layers/meta-raspberrypi"; do
    if ! grep -Fq "$layer" conf/bblayers.conf; then
        bitbake-layers add-layer "$layer"
    fi
done

mkdir -p "$build_dir/logs"
bitbake-layers show-layers | tee "$build_dir/logs/layers.log"
bitbake -p 2>&1 | tee "$build_dir/logs/parse.log"
bitbake core-image-minimal 2>&1 | tee "$build_dir/logs/build.log"
bitbake-getvar DEPLOY_DIR_IMAGE | tee "$build_dir/logs/deploy-path.log"

# Copy the actual image behind Yocto's unversioned deploy symlink.
deploy_dir="$(bitbake-getvar --value DEPLOY_DIR_IMAGE)"
machine="$(bitbake-getvar --value MACHINE)"
image="$(readlink -e -- "$deploy_dir/core-image-minimal-$machine.rootfs.wic.bz2")"
mkdir -p "$artifact_dir"
cp -- "$image" "$artifact_dir/"
printf 'Exported SD image: %s\n' "$artifact_dir/${image##*/}"
sha256sum -- "$artifact_dir/${image##*/}"
