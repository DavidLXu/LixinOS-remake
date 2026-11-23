#!/bin/sh
set -eu
mkdir -p build/net
cd build/net
tar -xzf ../../third_party/lwip-src.tar.gz
tar -xzf ../../third_party/bearssl-0.6.tar.gz
make -s -C bearssl-0.6 -j8 lib CC=aarch64-none-elf-gcc AR=aarch64-none-elf-ar CFLAGS="-Os -ffunction-sections -fdata-sections -DBR_USE_URANDOM=0 -DBR_USE_UNIX_TIME=0" >bearssl.log
cflags="-Os -ffunction-sections -fdata-sections -I../../net -Ilwip/src/include"
tar -tzf ../../third_party/lwip-src.tar.gz | awk '/^lwip\/src\/core\/(ipv4\/)?[^\/]*\.c$/ || /^lwip\/src\/netif\/ethernet\.c$/' > lwip.units
mkdir -p lwip-obj
while IFS= read -r src; do
    obj="$(basename "$src" .c).o"
    aarch64-none-elf-gcc $cflags -c "$src" -o "lwip-obj/$obj"
done < lwip.units
aarch64-none-elf-ar rcs lwip-next.a lwip-obj/*.o
mv lwip-next.a lwip.a
