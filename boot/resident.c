#include "abi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include "libtcc.h"

extern const u8 seed_begin[], seed_end[], source_begin[], source_end[];
extern void transfer(struct machine *, u64) __attribute__((noreturn));
extern void psci_poweroff(void);
static struct machine machine;
static unsigned cursor;
static int output_open;

/* Newlib I/O for the guest compiler. The linker output is a guest RAM file. */
int _open(const char *name, int flags, int mode) {
    (void)flags; (void)mode;
    if (strcmp(name, "kernel.elf") || output_open) { errno = ENOENT; return -1; }
    output_open = 1;
    cursor = machine.image_len = 0;
    return 3;
}
int _close(int fd) { if (fd != 3) return -1; output_open = 0; return 0; }
int _write(int fd, const char *p, int n) {
    if (fd == 1 || fd == 2) {
        for (int i = 0; i < n; ++i) {
            while (*(volatile u32 *)0x09000018 & 32) {}
            *(volatile u32 *)0x09000000 = (u8)p[i];
        }
        return n;
    }
    if (fd != 3 || !output_open || n < 0 || (unsigned)n > IMAGE_CAP - cursor) {
        errno = ENOSPC; return -1;
    }
    memcpy(machine.image + cursor, p, n);
    cursor += n;
    if (cursor > machine.image_len) machine.image_len = cursor;
    return n;
}
int _lseek(int fd, int offset, int origin) {
    long pos = offset;
    if (fd != 3) return -1;
    if (origin == SEEK_CUR) pos += cursor;
    if (origin == SEEK_END) pos += machine.image_len;
    if (pos < 0 || pos > IMAGE_CAP) return -1;
    return cursor = pos;
}
int _fstat(int fd, struct stat *s) {
    memset(s, 0, sizeof(*s)); s->st_mode = fd == 3 ? S_IFREG : S_IFCHR; return 0;
}
int _isatty(int fd) { return fd <= 2; }
void *_sbrk(ptrdiff_t n) {
    static char *top = (char *)0x41000000;
    char *old = top;
    if (n < 0 || (unsigned long)n > (unsigned long)((char *)0x41e00000 - top)) {
        errno = ENOMEM; return (void *)-1;
    }
    top += n; return old;
}
static void diagnostic(void *ignored, const char *s) {
    (void)ignored; _write(2, s, strlen(s)); _write(2, "\n", 1);
}
static int compile(struct machine *m, unsigned generation) {
    char value[24];
    TCCState *t = tcc_new();
    if (!t) return 0;
    snprintf(value, sizeof(value), "%u", generation);
    tcc_set_error_func(t, NULL, diagnostic);
    tcc_set_options(t, "-nostdlib -nostdinc -static -Wl,-Ttext=0x44010000");
    tcc_define_symbol(t, "BUILD_NUMBER", value);
    m->image_len = 0;
    int ok = tcc_set_output_type(t, TCC_OUTPUT_EXE) == 0
          && tcc_compile_string(t, m->source) == 0
          && tcc_output_file(t, "kernel.elf") == 0;
    tcc_delete(t);
    return ok && m->valid(m->image, m->image_len);
}

struct elf_header {
    u8 id[16]; u16 type, arch; u32 version;
    u64 entry, phoff, shoff; u32 flags;
    u16 size, phsize, count, shsize, shcount, names;
};
struct program_header { u32 type, flags; u64 offset, va, pa, file, mem, align; };
static int valid(const u8 *bytes, u32 size) {
    struct elf_header h;
    if (size < sizeof(h)) return 0;
    memcpy(&h, bytes, sizeof h);
    if (memcmp(h.id, "\177ELF\2\1\1", 7) || h.type != 2 || h.arch != 183 ||
        h.phsize != sizeof(struct program_header) || !h.count || h.count > 32 ||
        h.phoff > size || h.count * h.phsize > size - h.phoff || (h.entry & 3)) return 0;
    int executable = 0;
    for (unsigned i = 0; i < h.count; ++i) {
        struct program_header p;
        memcpy(&p, bytes + h.phoff + i * sizeof p, sizeof p);
        if (p.type != 1) continue;
        if (p.va < 0x44000000 || p.va > 0x44400000 || p.mem > 0x44400000 - p.va ||
            p.file > p.mem || p.offset > size || p.file > size - p.offset) return 0;
        if ((p.flags & 1) && h.entry >= p.va && h.entry - p.va < p.file) executable = 1;
    }
    return executable;
}
static void barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static void enter(struct machine *m) {
    struct elf_header h;
    if (!valid(m->image, m->image_len)) abort();
    memcpy(&h, m->image, sizeof h);
    for (unsigned i = 0; i < h.count; ++i) {
        struct program_header p;
        memcpy(&p, m->image + h.phoff + i * sizeof p, sizeof p);
        if (p.type != 1) continue;
        memcpy((void *)p.va, m->image + p.offset, p.file);
        memset((void *)(p.va + p.file), 0, p.mem - p.file);
    }
    barrier();
    transfer(m, h.entry);
}
void boot_main(void) {
    machine.abi = ABI_VERSION;
    machine.slot = -1;
    machine.compile = compile; machine.valid = valid;
    machine.enter = enter; machine.barrier = barrier;
    machine.poweroff = psci_poweroff;
    machine.image_len = seed_end - seed_begin;
    machine.source_len = source_end - source_begin;
    if (machine.image_len > IMAGE_CAP || machine.source_len >= SOURCE_CAP) abort();
    memcpy(machine.image, seed_begin, machine.image_len);
    memcpy(machine.source, source_begin, machine.source_len);
    enter(&machine);
}
char *realpath(const char *path, char *out) {
    (void)path; (void)out; errno = ENOENT; return NULL;
}
char *getcwd(char *out, size_t cap) {
    if (!out || cap < 2) { errno = ERANGE; return NULL; }
    out[0] = '/'; out[1] = 0; return out;
}
