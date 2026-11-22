#!/bin/sh
set -eu
mkdir -p build/net
cd build/net
tar -xzf ../../third_party/lwip-src.tar.gz
tar -xzf ../../third_party/bearssl-0.6.tar.gz
make -s -C bearssl-0.6 -j8 lib CC=aarch64-none-elf-gcc AR=aarch64-none-elf-ar CFLAGS="-Os -ffunction-sections -fdata-sections -DBR_USE_URANDOM=0 -DBR_USE_UNIX_TIME=0" >bearssl.log
cflags="-Os -ffunction-sections -fdata-sections -I../../net -Ilwip/src/include"
for src in lwip/src/core/*.c lwip/src/core/ipv4/*.c lwip/src/netif/ethernet.c; do
    obj="$(basename "$src" .c).o"
    aarch64-none-elf-gcc $cflags -c "$src" -o "$obj"
done
aarch64-none-elf-ar rcs lwip.a ./*.o
