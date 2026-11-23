#include "abi.h"
#include "api.h"
#include "platform.h"
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
static unsigned output_cap;

#include "source_io.c"

/* Newlib I/O for the guest compiler. The linker output is a guest RAM file. */
int _open(const char *name, int flags, int mode) {
    (void)mode;
    if ((flags & 3)==0) return source_open(name);
    if (!output_name || strcmp(name, output_name) || output_open) { errno = ENOENT; return -1; }
    output_open = 1;
    cursor = *output_length = 0;
    return 3;
}
int _close(int fd) { struct reader_fd *r=reader(fd); if (r) { r->data=NULL; return 0; } if (fd != 3) return -1; output_open = 0; return 0; }
int _write(int fd, const char *p, int n) {
    if (fd == 1 || fd == 2) {
        for (int i = 0; i < n; ++i) {
            while (*(volatile u32 *)0x09000018 & 32) {}
            *(volatile u32 *)0x09000000 = (u8)p[i];
        }
        return n;
    }
    if (fd != 3 || !output_open || n < 0 || (unsigned)n > output_cap - cursor) {
        errno = ENOSPC; return -1;
    }
    memcpy(output_data + cursor, p, n);
    cursor += n;
    if (cursor > *output_length) *output_length = cursor;
    return n;
}
int _lseek(int fd, int offset, int origin) {
    long pos = offset;
    struct reader_fd *r=reader(fd);
    if (r) {
        if (origin==SEEK_CUR) pos+=r->pos;
        else if (origin==SEEK_END) pos+=r->size;
        if (pos<0 || pos>r->size) return -1;
        return r->pos=pos;
    }
    if (fd != 3) return -1;
    if (origin == SEEK_CUR) pos += cursor;
    if (origin == SEEK_END) pos += *output_length;
    if (pos < 0 || pos > output_cap) return -1;
    return cursor = pos;
}
int _fstat(int fd, struct stat *s) {
    memset(s, 0, sizeof(*s)); s->st_mode = fd == 3 ? S_IFREG : S_IFCHR; return 0;
}
int _isatty(int fd) { return fd <= 2; }
void *_sbrk(ptrdiff_t n) {
    static char *top = (char *)0x4a000000;
    char *old = top;
    if (n < 0 || (unsigned long)n > (unsigned long)((char *)0x4e000000 - top)) {
        errno = ENOMEM; return (void *)-1;
    }
    top += n; return old;
}
static void diagnostic(void *ignored, const char *s) {
    (void)ignored; snprintf(machine.diagnostic,sizeof machine.diagnostic,"%s",s); _write(2, s, strlen(s)); _write(2, "\n", 1);
}
static int add_inputs(TCCState *t,const char *path) {
    unsigned n=strlen(path);
    if (n<6 || strcmp(path+n-6,".build")) return tcc_add_file(t,path)==0;
    unsigned bytes;
    const char *p=source_file(path,&bytes);
    if (!p) return 0;
    while (*p) {
        const char *end=strchr(p,'\n'); char filename[256];
        if (!end || end==p || (unsigned)(end-p)>=sizeof filename) return 0;
        memcpy(filename,p,end-p); filename[end-p]=0; p=end+1;
        if (tcc_add_file(t,filename)<0) return 0;
    }
    return 1;
}
static unsigned app_builds;
static int compile_input(const char *path,unsigned generation,int user) {
    if (user) ++app_builds;
    char value[24];
    machine.diagnostic[0] = 0;
    TCCState *t = tcc_new();
    if (!t) return 0;
    snprintf(value, sizeof(value), "%u", generation);
    tcc_set_error_func(t, NULL, diagnostic);
    tcc_set_options(t, user ? "-nostdlib -nostdinc -static -Wl,-Ttext=0x46010000" :
                             "-nostdlib -nostdinc -static -Wl,-Ttext=0x44010000");
    tcc_define_symbol(t, "BUILD_NUMBER", value);
    tcc_add_include_path(t,"/src/user"); tcc_add_include_path(t,"/src/boot");
    tcc_add_include_path(t,"/src/sdk");
    output_cap=IMAGE_CAP;
    output_data = user ? tool_image : machine.image;
    output_length = user ? &tool_size : &machine.image_len;
    output_name = user ? "tools.elf" : "kernel.elf";
    *output_length = 0;
    int ok = tcc_set_output_type(t, TCC_OUTPUT_EXE) == 0
          && add_inputs(t,path)
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
    const char *path=strcmp(m->source,source_at(sources->entries[2].offset)) ?
        "/src/kernel.c":"/src/kernel/main.c";
    return compile_input(path,generation,0) && valid(m->image,m->image_len);
}
static void barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
volatile unsigned trial_armed;
static void end_trial(void) {
    __asm__ volatile(A64_mask_irq "\n" A64_stop_timer "\nisb" ::: "memory");
    trial_armed=0;
}
static void arm_timer(unsigned seconds) {
    /* GICv2, non-secure EL1 physical timer PPI 30 on QEMU virt. */
    volatile u32 *dist=(volatile u32 *)0x08000000, *cpu=(volatile u32 *)0x08010000;
    dist[0]=1; dist[0x100/4]=1u<<30;
    ((volatile u8 *)dist)[0x400+30]=0x80;
    cpu[1]=0xff; cpu[0]=1;
    cpu_timer(cpu_ticks()+seconds*cpu_frequency());
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
        trial_armed=1; arm_timer(8); __asm__ volatile(A64_unmask_irq);
    } else end_trial();
    transfer(m, h.entry);
}
#include "selfbuild.c"
#include "user_loader.c"
#include "packages.c"
void boot_main(void) {
    if (!source_valid()) { _write(2,"invalid source volume\n",22); abort(); }
    machine.abi = ABI_VERSION;
    machine.tool_seed=source_at(sources->entries[3].offset);
    machine.build_system=build_system;
    machine.compile_app=compile_app;
    machine.compile_apps=compile_apps; machine.packages=packages;
    machine.system_buffer=system_image; machine.boot_system=boot_system;
    u32 fingerprint=2166136261U;
    for (unsigned i=0;i<sources->bytes;++i) fingerprint=(fingerprint^((const u8 *)sources)[i])*16777619U;
    machine.volume_id=fingerprint;
    machine.tool_compile=tool_compile; machine.tool_run=tool_run;
    machine.slot = -1;
    machine.compile = compile; machine.valid = valid;
    machine.enter = enter; machine.barrier = barrier;
    machine.poweroff = psci_poweroff;
    machine.propose = propose; machine.set_key = set_key;
    machine.probe=probe; machine.end_trial=end_trial;
    machine.upgrade = *(volatile u32 *)0x41fff000 == 0x123afecd;
#ifdef GUEST_BUILD
    machine.guest_resident=(u64)boot_main>=0x42000000;
    machine.source_len=sources->entries[2].size;
    if (machine.source_len>=SOURCE_CAP) abort();
    memcpy(machine.source,source_at(sources->entries[2].offset),machine.source_len+1);
    if (!compile(&machine,0)) abort();
    puts(machine.guest_resident ? "boot: guest-built resident and TinyCC (system)" :
                                 "boot: guest-built resident and TinyCC (recovery)");
#else
    machine.image_len = seed_end - seed_begin;
    machine.source_len = source_end - source_begin;
    if (machine.image_len > IMAGE_CAP || machine.source_len >= SOURCE_CAP) abort();
    memcpy(machine.image, seed_begin, machine.image_len);
    memcpy(machine.source, source_begin, machine.source_len);
#endif
    enter(&machine);
}
char *realpath(const char *path, char *out) {
    (void)path; (void)out; errno = ENOENT; return NULL;
}
char *getcwd(char *out, size_t cap) {
    if (!out || cap < 2) { errno = ERANGE; return NULL; }
    out[0] = '/'; out[1] = 0; return out;
}
