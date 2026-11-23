"""Rebuild all targets, boot the guest-built ELF on an empty disk, then reproduce it."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import tempfile

from test import ROOT, boot, expect


def fnv(data):
    value=2166136261
    for byte in data:
        value=((value^byte)*16777619)&0xffffffff
    return value


def read_resident(disk,sector):
    with disk.open('rb') as f:
        f.seek(sector*512)
        header=f.read(24)
        magic,volume,revision,size,checksum,guard=struct.unpack('<6I',header)
        assert magic==0x53595332 and volume and revision and fnv(header[:20])==guard
        assert 0<size<=6*1024*1024
        f.seek((sector+1)*512)
        image=f.read(size)
        assert len(image)==size and fnv(image)==checksum
        assert image[:7]==b'\x7fELF\x02\x01\x01'
        return image


def check_programs(console):
    expect(console,'cd /src/user','kernel> ')
    expect(console,'pwd','\n/src/user\n')
    expect(console,'ls','hello.build')
    expect(console,'read ../boot/platform.c 1 1','Page tables')
    expect(console,'grep tcc_new /src/tinycc/libtcc.c','tcc_new(')
    expect(console,'tcc /src/user/hello.build','application ELF ready')
    expect(console,'run reader','\nhello reader\n')
    expect(console,'ls /src/musl','internal/')
    expect(console,'run again','\nhello again\n')
    expect(console,'tcc /src/validation/numbers.build','application ELF ready')
    expect(console,'run','floating constants verified')
    expect(console,'cd /','kernel> ')


def exercise(temp,seed,volume,export):
    disk=temp/'work.img'
    os.environ['LIXINOS_BOOT_IMAGE']=str(seed)
    os.environ['LIXINOS_SOURCE_VOLUME']=str(volume)
    with boot(disk,timeout=300) as c:
        c();check_programs(c)
        expect(c,'tcc all','TCC: bootable system ELF saved')
        expect(c,'switch','build=1 slot=B hot=1')
        expect(c,'tcc recovery','TCC: recovery ELF saved')
    image=read_resident(disk,2048+16384)
    recovery=read_resident(disk,34816)
    direct=temp/'system.elf'
    direct.write_bytes(image)
    recovery_file=temp/"recovery.elf"
    recovery_file.write_bytes(recovery)
    # The disk boot dispatcher picks the committed guest resident.
    with boot(disk,timeout=300) as c:
        expect(c,None,'boot: guest-built resident and TinyCC (system)')
        expect(c,'version','build=1 slot=B hot=0')
    # No old executable or old disk is supplied to QEMU in this stage.
    os.environ['LIXINOS_BOOT_IMAGE']=str(direct)
    fresh=temp/'fresh.img'
    with boot(fresh,timeout=300) as c:
        expect(c,None,'boot: guest-built resident and TinyCC (system)')
        check_programs(c)
        expect(c,'tcc all','TCC: bootable system ELF saved')
        expect(c,'switch','build=1 slot=B hot=1')
        expect(c,'tcc all','TCC: bootable system ELF saved')
    latest,previous=read_resident(fresh,2048),read_resident(fresh,2048+16384)
    for generation,data in enumerate((image,previous,latest),1):
        print(f'system generation {generation} sha256 {hashlib.sha256(data).hexdigest()}',flush=True)
    assert latest==previous==image
    os.environ['LIXINOS_BOOT_IMAGE']=str(recovery_file)
    with fresh.open('r+b') as f:
        f.seek(2049*512); f.write(b'BAD!')
    with boot(fresh,timeout=300) as c:
        expect(c,None,'boot: guest-built resident and TinyCC (system)')
    with fresh.open('r+b') as f:
        f.seek((2049+16384)*512); f.write(b'BAD!')
    with boot(fresh,timeout=300) as c:
        banner=c()
        assert 'boot: guest-built resident and TinyCC (recovery)' in banner and '(system)' not in banner
        expect(c,'version','build=1 slot=B hot=0')
    print(f'PASS: guest-built standalone ELF ({len(image)} bytes), byte-identical rebuilds, EL0 applications')
    if export:
        with tempfile.NamedTemporaryFile(dir=export.parent,delete=False) as output:
            pending=Path(output.name)
            try:
                output.write(recovery)
                output.flush()
                os.fsync(output.fileno())
                os.replace(pending,export)
            finally:
                pending.unlink(missing_ok=True)
        print(f'Exported guest-built recovery ELF ({len(recovery)} bytes)')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--seed',type=Path,default=ROOT/'release/boot.elf')
    parser.add_argument('--sources',type=Path,default=ROOT/'release/sources.img')
    parser.add_argument('--export-recovery',type=Path)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        exercise(Path(tmp),args.seed.resolve(),args.sources.resolve(),args.export_recovery)
