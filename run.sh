#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
image=build/boot.bin
if [ ! -f "$image" ]; then
    (cd release
     if command -v sha256sum >/dev/null; then sha256sum -c SHA256SUMS >&2
     else shasum -a 256 -c SHA256SUMS >&2; fi)
    image=release/boot.bin
fi
upgrade=
if [ "${1:-}" = "--upgrade" ]; then
    upgrade="-device loader,addr=0x41fff000,data=0x123afecd,data-len=4"
    shift
fi
disk=${1:-build/system.img}
if [ ! -e "$disk" ]; then
    tmp=$(mktemp "${disk}.XXXXXX")
    trap 'rm -f "$tmp"' EXIT
    dd if=/dev/zero of="$tmp" bs=1048576 count=1 2>/dev/null
    ln "$tmp" "$disk"
    rm -f "$tmp"; trap - EXIT
fi
exec qemu-system-aarch64 -M virt -cpu cortex-a57 -m 128M -smp 1 \
    -display none -serial stdio -monitor none -netdev user,id=guestnet -device virtio-net-device,netdev=guestnet,bus=virtio-mmio-bus.1 \
    -device virtio-rng-device,bus=virtio-mmio-bus.2 $upgrade -kernel "$image" \
    -global virtio-mmio.force-legacy=false -drive "if=none,id=sys,file=$disk,format=raw" \
    -device virtio-blk-device,drive=sys,bus=virtio-mmio-bus.0
