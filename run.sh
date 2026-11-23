#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
image=release/boot.elf
volume=release/sources.img
if [ "${1:-}" = "--seed" ]; then
    image=build/boot.bin
    volume=build/sources.img
    shift
elif [ -z "${LIXINOS_BOOT_IMAGE:-}" ] && [ -z "${LIXINOS_SOURCE_VOLUME:-}" ]; then
    (cd release
     if command -v sha256sum >/dev/null; then sha256sum -c SHA256SUMS >&2
     else shasum -a 256 -c SHA256SUMS >&2; fi)
fi
image=${LIXINOS_BOOT_IMAGE:-$image}
volume=${LIXINOS_SOURCE_VOLUME:-$volume}
upgrade=
if [ "${1:-}" = "--upgrade" ]; then
    upgrade="-device loader,addr=0x41fff000,data=0x123afecd,data-len=4"
    shift
fi
disk=${1:-build/system.img}
if [ ! -e "$disk" ]; then
    tmp=$(mktemp "${disk}.XXXXXX")
    trap 'rm -f "$tmp"' EXIT
    dd if=/dev/zero of="$tmp" bs=1048576 count=25 2>/dev/null
    ln "$tmp" "$disk"
    rm -f "$tmp"; trap - EXIT
fi
if [ -n "$upgrade" ] && [ "$(wc -c < "$disk")" -eq 1048576 ]; then
    dd if=/dev/zero of="$disk" bs=1048576 seek=24 count=1 conv=notrunc 2>/dev/null
fi
exec qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M -smp 1 \
    -display none -serial stdio -monitor none -netdev user,id=guestnet -device virtio-net-device,netdev=guestnet,bus=virtio-mmio-bus.1 \
    -device virtio-rng-device,bus=virtio-mmio-bus.2 $upgrade -kernel "$image" \
    -device "loader,file=$volume,addr=0x4e000000,force-raw=on" \
    -global virtio-mmio.force-legacy=false -drive "if=none,id=sys,file=$disk,format=raw" \
    -device virtio-blk-device,drive=sys,bus=virtio-mmio-bus.0
