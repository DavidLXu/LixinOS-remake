"""Exercise guest-built EL0 commands, their independent source banks and old-disk upgrade."""
import gzip
from pathlib import Path
import tempfile

from test import ROOT, boot, expect


def exercise(disk):
    with boot(disk) as c:
        expect(c, None, "1.4.0 build=0 slot=A")
        expect(c, "tcc tools", "TCC: EL0 tools ELF ready")
        expect(c, "ls", "\nsrc/\n")
        expect(c, "cd ../../src/.", "kernel> ")
        expect(c, "pwd", "\n/src\n")
        expect(c, "ls", "\nkernel.c\ntools.c\n")
        expect(c, "cd kernel.c", "not a directory")
        expect(c, "pwd", "\n/src\n")
        expect(c, "read tools.c 1 1", "/* Shared with the single foreground EL0 tool.")
        expect(c, "grep tool_input tools.c", "2:struct tool_input")
        expect(c, "cat tools.c", "void _start(struct tool_input")
        # Building a user image between a kernel build and its switch must preserve
        # the staged kernel ELF. The cwd belongs to the calling shell.
        expect(c, "build", "TCC: ELF ready")
        expect(c, "tcc tools", "TCC: EL0 tools ELF ready")
        expect(c, "switch", "build=1 slot=B hot=1")
        expect(c, "pwd", "\n/src\n")
        expect(c, 'tool edit print("src/|print("src/ (user build)', "tool draft changed")
        expect(c, "tcc tools", "TCC: EL0 tools ELF ready")
        expect(c, "ls /", "\nsrc/ (user build)\n")
        expect(c, "version", "build=1 slot=B hot=1")
    with boot(disk) as c:
        expect(c, None, "build=1 slot=B hot=0")
        expect(c, "pwd", "\n/\n")
        expect(c, "ls", "\nsrc/ (user build)\n")
        expect(c, "tool edit command(in);|invalid @;", "tool draft changed")
        expect(c, "tcc tools", "tool build failed")
        expect(c, "tool reset", "tool draft reloaded")
        expect(c, "ls", "\nsrc/ (user build)\n")
        # An invalid user instruction and a non-returning user command each return
        # control to the same kernel, which can still edit and recompile the tool.
        for statement, outcome in [('__asm__ volatile(".long 0");', "user program fault"),
                                   ("for (;;) {}", "user program timed out")]:
            expect(c, "tool edit command(in);|" + statement, "tool draft changed")
            expect(c, "tcc tools", "TCC: EL0 tools ELF ready")
            expect(c, "pwd", outcome)
            expect(c, "version", "build=1 slot=B hot=0")
            expect(c, "tool edit " + statement + "|command(in);", "tool draft changed")
            expect(c, "tcc tools", "TCC: EL0 tools ELF ready")
            expect(c, "pwd", "\n/\n")


def upgrade(disk):
    disk.write_bytes(gzip.decompress((ROOT / "release/harness-demo.img.gz").read_bytes()))
    with boot(disk, launcher=[ROOT / "run.sh", "--upgrade"]) as c:
        expect(c, None, "Kernel 1.4.0")
        expect(c, "ls /src", "\nkernel.c\ntools.c\n")
        expect(c, "rollback", "Kernel 1.1.0")
        expect(c, "color", "\nblue\n")
    with boot(disk) as c:
        expect(c, None, "Kernel 1.1.0")
        expect(c, "color", "\nblue\n")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        exercise(Path(tmp) / "user.img")
        upgrade(Path(tmp) / "old.img")
    print("PASS: guest TCC user ELF, six commands, independent edits, fault, timeout and old-disk rollback")
