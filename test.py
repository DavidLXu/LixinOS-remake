"""Exercise the release on a private disk with a real QEMU serial console."""
import contextlib
import os
from pathlib import Path
import selectors
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent


@contextlib.contextmanager
def boot(disk, launcher=None, timeout=20):
    vm = subprocess.Popen([*(launcher or [ROOT / "run.sh"]), disk], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    with selectors.DefaultSelector() as selector:
        selector.register(vm.stdout, selectors.EVENT_READ)

        def prompt(command=None, *, secret=None):
            if command is not None:
                vm.stdin.write(command.encode() + b"\r")
                vm.stdin.flush()
            deadline = time.monotonic() + timeout
            output = b""
            terminator = b"\n}\nkernel> " if command == "source" else b"kernel> "
            while not output.endswith(terminator):
                if not selector.select(max(0, deadline - time.monotonic())):
                    raise AssertionError(("console timeout", output[-600:]))
                chunk = os.read(vm.stdout.fileno(), 16384)
                if not chunk:
                    raise AssertionError(("early exit", vm.poll(), output[-600:]))
                output += chunk
                if secret is not None and output.endswith(b"API key (hidden): "):
                    vm.stdin.write(secret.encode() + b"\r")
                    vm.stdin.flush()
                    secret = None
            result = output.decode()
            if command != "source":
                print(result.removesuffix("kernel> ").rstrip(), flush=True)
            return result

        try:
            yield prompt
            vm.stdin.write(b"off\r")
            vm.stdin.flush()
            assert vm.wait(timeout=10) == 0
        finally:
            if vm.poll() is None:
                vm.kill()
                vm.wait()
            vm.stdin.close()
            vm.stdout.close()


def expect(console, command, result):
    out = console(command)
    assert result in out, (command, result, out[-600:])


def disk_record(disk, slot):
    base = slot * 1024 * 512
    with disk.open("rb") as f:
        f.seek(base)
        epoch, magic, abi, size, source_size = struct.unpack("<QIIII", f.read(24))
        assert epoch > 0 and magic == 0x31584B4C and abi in (2, 3, 4, 5)
        f.seek(base + 512)
        image = f.read(size)
        assert image[:7] == b"\x7fELF\x02\x01\x01" and image[18:20] == b"\xb7\0"
        f.seek(base + 513 * 512)
        return image, f.read(source_size)


def exercise(disk):
    with boot(disk) as c:
        expect(c, None, "build=0 slot=A hot=0")
        expect(c, "switch", "switch refused")
        expect(c, "replace hello-kernel|first-self-build", "source changed")
        expect(c, "note live-session", "kernel> ")
        expect(c, "build", "TCC: ELF ready")
        expect(c, "switch", "build=1 slot=B hot=1 message=first-self-build")
        expect(c, "note", "note=live-session")
        expect(c, "rollback", "build=0 slot=A hot=2 message=hello-kernel")
        expect(c, "rollback", "build=1 slot=B hot=3 message=first-self-build")
        expect(c, "replace void _start(|void @(", "source changed")
        expect(c, "build", "TCC: build failed")
        expect(c, "switch", "switch refused")
        expect(c, "version", "build=1 slot=B")
    with boot(disk) as c:
        expect(c, None, "build=1 slot=B hot=0 message=first-self-build")
        assert "note=\n" in c("note")
        assert "void _start(" in c("source")
        expect(c, "replace first-self-build|second-self-build", "source changed")
        expect(c, "build", "TCC: ELF ready")
        expect(c, "switch", "build=2 slot=A hot=1 message=second-self-build")
    img1, src1 = disk_record(disk, 1)
    img2, src2 = disk_record(disk, 0)
    seed_source = (ROOT / "src/abi.h").read_bytes() + (ROOT / "src/kernel.c").read_bytes().replace(
        b'#include "harness.c"', (ROOT / "src/harness.c").read_bytes())
    assert src1 == seed_source.replace(b"hello-kernel", b"first-self-build", 1)
    assert src2 == src1.replace(b"first-self-build", b"second-self-build", 1)
    assert img1 != img2 and b"message=second-self-build" in img2
    with boot(disk) as c:
        expect(c, None, "build=2 slot=A hot=0")
        expect(c, "rollback", "build=1 slot=B hot=1")
    with boot(disk) as c:
        expect(c, None, "build=1 slot=B hot=0")
    # Damage the latest ELF on this throwaway disk, leaving its header intact.
    with disk.open("r+b") as f:
        f.seek((1024 + 1) * 512)
        f.write(b"BAD!")
    with boot(disk) as c:
        expect(c, None, "build=2 slot=A hot=0")
    print("PASS: guest TCC, two generations, RAM handoff, persistent rollback, corrupt-slot fallback")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        exercise(Path(tmp) / "test.img")
