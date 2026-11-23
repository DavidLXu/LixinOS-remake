#include "abi.h"
#include "api.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include "libtcc.h"

extern const u8 seed_begin[], seed_end[], source_begin[], source_end[];
extern void transfer(struct machine *, u64) __attribute__((noreturn));
extern void psci_poweroff(void);
extern int propose(const char *, const char *, const char *, const char *, const char *,
                   const char *, const char *, int, struct proposal *);
extern int probe(const char *,const char *,char *,unsigned);
extern void set_key(const char *, const char *);
static struct machine machine;
static unsigned cursor;
static int output_open;
static u8 tool_image[IMAGE_CAP];
static unsigned tool_size;
static u8 *output_data;
static unsigned *output_length;
static const char *output_name;

/* Newlib I/O for the guest compiler. The linker output is a guest RAM file. */
int _open(const char *name, int flags, int mode) {
    (void)flags; (void)mode;
    if (!output_name || strcmp(name, output_name) || output_open) { errno = ENOENT; return -1; }
    output_open = 1;
    cursor = *output_length = 0;
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
    memcpy(output_data + cursor, p, n);
    cursor += n;
    if (cursor > *output_length) *output_length = cursor;
    return n;
}
int _lseek(int fd, int offset, int origin) {
    long pos = offset;
    if (fd != 3) return -1;
    if (origin == SEEK_CUR) pos += cursor;
    if (origin == SEEK_END) pos += *output_length;
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
    (void)ignored; snprintf(machine.diagnostic,sizeof machine.diagnostic,"%s",s); _write(2, s, strlen(s)); _write(2, "\n", 1);
}
static int compile_source(const char *source, unsigned generation, int user) {
    char value[24];
    machine.diagnostic[0] = 0;
    TCCState *t = tcc_new();
    if (!t) return 0;
    snprintf(value, sizeof(value), "%u", generation);
    tcc_set_error_func(t, NULL, diagnostic);
    tcc_set_options(t, user ? "-nostdlib -nostdinc -static -Wl,-Ttext=0x46010000" :
                             "-nostdlib -nostdinc -static -Wl,-Ttext=0x44010000");
    tcc_define_symbol(t, "BUILD_NUMBER", value);
    output_data = user ? tool_image : machine.image;
    output_length = user ? &tool_size : &machine.image_len;
    output_name = user ? "tools.elf" : "kernel.elf";
    *output_length = 0;
    int ok = tcc_set_output_type(t, TCC_OUTPUT_EXE) == 0
          && tcc_compile_string(t, source) == 0
          && tcc_output_file(t, output_name) == 0;
    tcc_delete(t);
    return ok;
}

struct elf_header {
    u8 id[16]; u16 type, arch; u32 version;
    u64 entry, phoff, shoff; u32 flags;
    u16 size, phsize, count, shsize, shcount, names;
};
struct program_header { u32 type, flags; u64 offset, va, pa, file, mem, align; };
static int valid_range(const u8 *bytes, u32 size, u64 low, u64 high) {
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
        if (p.va < low || p.va > high || p.mem > high - p.va ||
            p.file > p.mem || p.offset > size || p.file > size - p.offset) return 0;
        if ((p.flags & 1) && h.entry >= p.va && h.entry - p.va < p.file) executable = 1;
    }
    return executable;
}
static int valid(const u8 *bytes, u32 size) {
    return valid_range(bytes,size,0x44000000,0x44400000);
}
static int compile(struct machine *m, unsigned generation) {
    return compile_source(m->source,generation,0) && valid(m->image,m->image_len);
}
static void barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
volatile unsigned trial_armed;
static void end_trial(void) {
    __asm__ volatile("msr daifset,#2\nmsr cntp_ctl_el0,xzr\nisb" ::: "memory");
    trial_armed=0;
}
static void arm_timer(unsigned seconds) {
    /* GICv2, non-secure EL1 physical timer PPI 30 on QEMU virt. */
    volatile u32 *dist=(volatile u32 *)0x08000000, *cpu=(volatile u32 *)0x08010000;
    dist[0]=1; dist[0x100/4]=1u<<30;
    ((volatile u8 *)dist)[0x400+30]=0x80;
    cpu[1]=0xff; cpu[0]=1;
    u64 freq, now;
    __asm__ volatile("mrs %0,cntfrq_el0":"=r"(freq));
    __asm__ volatile("mrs %0,cntpct_el0":"=r"(now));
    u64 limit=now+seconds*freq, enable=1;
    __asm__ volatile("msr cntp_cval_el0,%0\nmsr cntp_ctl_el0,%1\nisb" ::
                     "r"(limit),"r"(enable):"memory");
}
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
    if (m->abi>=2 && m->trial==1) {
        trial_armed=1; arm_timer(8); __asm__ volatile("msr daifclr,#2");
    } else end_trial();
    transfer(m, h.entry);
}
#include "user_loader.c"
void boot_main(void) {
    machine.abi = ABI_VERSION;
    machine.tool_seed=tool_source; machine.tool_compile=tool_compile; machine.tool_run=tool_run;
    machine.slot = -1;
    machine.compile = compile; machine.valid = valid;
    machine.enter = enter; machine.barrier = barrier;
    machine.poweroff = psci_poweroff;
    machine.propose = propose; machine.set_key = set_key;
    machine.probe=probe; machine.end_trial=end_trial;
    machine.upgrade = *(volatile u32 *)0x41fff000 == 0x123afecd;
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
