#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p deps build
rev=f830a1697e159056b75de40a60464bc43ee670a3
if [ ! -d deps/tinycc/.git ]; then
    git init -q deps/tinycc
fi
if ! git -C deps/tinycc cat-file -e "$rev^{commit}" 2>/dev/null; then
    git -C deps/tinycc fetch --depth=1 https://github.com/TinyCC/tinycc.git "$rev"
fi
git -C deps/tinycc cat-file -e "$rev^{commit}"
if [ -d deps/arm-gnu ]; then PATH="$PWD/deps/arm-gnu/bin:$PATH"; fi
if ! command -v aarch64-none-elf-gcc >/dev/null; then
    if [ "$(uname -sm)" != "Darwin arm64" ]; then
        echo "Install Arm GNU aarch64-none-elf with newlib, then run build.sh." >&2; exit 1
    fi
    name=arm-gnu-toolchain-14.3.rel1-darwin-arm64-aarch64-none-elf
    curl -fL "https://developer.arm.com/-/media/Files/downloads/gnu/14.3.rel1/binrel/$name.tar.xz" -o build/arm.tar.xz
    echo '8636ad0e42acab8227974c8180916b114040f5c77f8b7f5480fe58af4911d484  build/arm.tar.xz' | shasum -a 256 -c -
    tar -xf build/arm.tar.xz -C deps
    mv "deps/$name" deps/arm-gnu
    PATH="$PWD/deps/arm-gnu/bin:$PATH"
fi
export PATH
cc=aarch64-none-elf-gcc
flags="-Os -ffunction-sections -fdata-sections -fno-stack-protector -fno-pie"
python3 - <<'PY_SOURCE'
from pathlib import Path
kernel = Path('src/kernel.c').read_text().replace('#include "harness.c"', Path('src/harness.c').read_text())
Path('build/kernel.c').write_text(Path('src/abi.h').read_text() + kernel)
PY_SOURCE
$cc $flags -ffreestanding -nostdlib -Wl,-T,boot/kernel.ld,--build-id=none build/kernel.c -o build/seed.elf
aarch64-none-elf-strip build/seed.elf
cat > build/assets.S <<'ASM'
.section .rodata.assets,"a"
.global seed_begin, seed_end, source_begin, source_end
.balign 16
seed_begin: .incbin "build/seed.elf"
seed_end:
source_begin: .incbin "build/kernel.c"
source_end:
ASM
# Only the ELF compiler/linker is needed. Omit TinyCC's POSIX in-process runner.
mkdir -p build/tinycc
git -C deps/tinycc archive "$rev" | tar -xf - -C build/tinycc
python3 - <<'PY'
from pathlib import Path
p = Path('build/tinycc/tcc.h')
s = p.read_text()
key = '#if defined _WIN32 == defined TCC_TARGET_PE'
assert s.count(key) == 1
p.write_text(s.replace(key, '#if 0 && defined _WIN32 == defined TCC_TARGET_PE'))
PY
cc -DC2STR build/tinycc/conftest.c -o build/c2str
build/c2str build/tinycc/include/tccdefs.h build/tinycc/tccdefs_.h
$cc $flags -Iboot -Ibuild/tinycc -c build/tinycc/libtcc.c -o build/tcc.o
./net/build.sh
$cc $flags -Isrc -Inet -Ithird_party -Ibuild/net/bearssl-0.6/inc -Ibuild/net/lwip/src/include -Ibuild/tinycc -nostartfiles -Wl,--gc-sections,--build-id=none,-T,boot/resident.ld \
    boot/start.S boot/resident.c build/assets.S build/tcc.o net/transport.c net/provider.c third_party/cJSON.c build/net/lwip.a build/net/bearssl-0.6/build/libbearssl.a -lc -lm -lnosys -lgcc -o build/boot.elf
aarch64-none-elf-objcopy -O binary build/boot.elf build/boot.bin
