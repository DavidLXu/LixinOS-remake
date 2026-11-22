#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
image=build/boot.bin
if [ ! -f "$image" ]; then
    (cd release && shasum -a 256 -c SHA256SUMS >&2)
    image=release/boot.bin
fi
disk=${1:-build/system.img}
if [ ! -e "$disk" ]; then
    python3 -c "import sys; open(sys.argv[1], 'xb').truncate(1048576)" "$disk"
fi
exec qemu-system-aarch64 -M virt -cpu cortex-a57 -m 128M -smp 1 \
    -display none -serial stdio -monitor none -nic none -kernel "$image" \
    -global virtio-mmio.force-legacy=false -drive "if=none,id=sys,file=$disk,format=raw" \
    -device virtio-blk-device,drive=sys,bus=virtio-mmio-bus.0
