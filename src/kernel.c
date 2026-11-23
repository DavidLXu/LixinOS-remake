#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif
/* The build embeds abi.h followed by this file as one editable translation unit. */
static struct machine *m;
static char observed[512];
static int capturing, observed_len, overflow;
static void put(char c) {
    if (capturing) {
        if (observed_len + 1 < (int)sizeof observed) observed[observed_len++] = c;
        else overflow = 1;
        return;
    }
    while (*(volatile u32 *)0x09000018 & 32) {}
    *(volatile u32 *)0x09000000 = (u8)c;
}
static void say(const char *s) { while (*s) put(*s++); }
static void number(u64 n) { if (n >= 10) number(n / 10); put('0' + n % 10); }
static unsigned length(const char *s) { unsigned i = 0; while (s[i]) ++i; return i; }
static int prefix(const char *s, const char *p) {
    while (*p) if (*s++ != *p++) return 0;
    return 1;
}
static int same(const char *a, const char *b) { return prefix(a, b) && length(a) == length(b); }
static void copy(u8 *to, const u8 *from, u32 n) { while (n--) *to++ = *from++; }
/* TCC lowers structure assignments and initializers to these freestanding routines. */
void *memcpy(void *to, const void *from, u64 n) {
    u8 *dst=to; const u8 *src=from;
    while (n--) *dst++=*src++;
    return to;
}
void *memset(void *to, int value, u64 n) {
    u8 *p=to;
    while (n--) *p++=(u8)value;
    return to;
}
void *memmove(void *to, const void *from, u64 n) {
    u8 *dst=to; const u8 *src=from;
    if ((u64)dst <= (u64)src) { while (n--) *dst++=*src++; }
    else { while (n) { --n; dst[n]=src[n]; } }
    return to;
}
static u32 digest(const void *data, u32 n) {
    const u8 *p = data;
    u32 h = 2166136261U;
    while (n--) h = (h ^ *p++) * 16777619U;
    return h;
}
static int input_line(char *s, unsigned cap, int echo) {
    unsigned n = 0;
    int full = 0;
    for (;;) {
        while (*(volatile u32 *)0x09000018 & 16) {}
        char c = *(volatile u32 *)0x09000000;
        if (c == '\r' || c == '\n') { put('\n'); s[n] = 0; return !full; }
        if (c == 8 || c == 127) {
            if (n) { --n; if (echo) say("\b \b"); }
        } else if (n + 1 < cap) { s[n++] = c; if (echo) put(c); }
        else full = 1;
    }
}

/* QEMU virt, modern VirtIO MMIO block device, synchronous split queue. */
static volatile u32 *regs;
struct descriptor { u64 addr; u32 len; u16 flags, next; };
static struct descriptor descriptors[8] __attribute__((aligned(16)));
static volatile struct { u16 flags, idx, ring[8], event; } avail __attribute__((aligned(2)));
static volatile struct { u16 flags, idx; struct { u32 id, len; } ring[8]; u16 event; } used __attribute__((aligned(4)));
static struct { u32 type, reserved; u64 sector; } request;
static volatile u8 status_byte;
static u8 sector_buf[512];
static int disk_init(void) {
    int i;
    for (i = 0; i < 32; ++i) {
        regs = (volatile u32 *)(0x0a000000UL + i * 512);
        if (regs[0] == 0x74726976 && regs[1] == 2 && regs[2] == 2) break;
    }
    if (i == 32) return 0;
    regs[0x70/4] = 0;
    while (regs[0x70/4]) {}
    regs[0x70/4] = 3;
    regs[0x14/4] = 0;
    if (!(regs[0x10/4] & (1 << 9))) return 0; /* durable flush */
    regs[0x24/4] = 0; regs[0x20/4] = 1 << 9;
    regs[0x14/4] = 1;
    if (!(regs[0x10/4] & 1)) return 0;
    regs[0x24/4] = 1; regs[0x20/4] = 1;
    regs[0x70/4] = 11;
    if (!(regs[0x70/4] & 8)) return 0;
    regs[0x30/4] = 0;
    if (regs[0x34/4] < 8) return 0;
    regs[0x38/4] = 8;
    avail.idx = used.idx = 0;
    avail.flags = 1;
    regs[0x80/4] = (u64)descriptors; regs[0x84/4] = 0;
    regs[0x90/4] = (u64)&avail; regs[0x94/4] = 0;
    regs[0xa0/4] = (u64)&used; regs[0xa4/4] = 0;
    regs[0x44/4] = 1; regs[0x70/4] = 15;
    return 1;
}
static int io(u32 type, u64 sector) {
    u16 before = used.idx;
    request.type = type; request.sector = sector;
    status_byte = 255;
    descriptors[0].addr = (u64)&request;
    descriptors[0].len = 16; descriptors[0].flags = 1; descriptors[0].next = type == 4 ? 2 : 1;
    descriptors[1].addr = (u64)sector_buf;
    descriptors[1].len = 512; descriptors[1].flags = type == 0 ? 3 : 1; descriptors[1].next = 2;
    descriptors[2].addr = (u64)&status_byte;
    descriptors[2].len = 1; descriptors[2].flags = 2;
    avail.ring[avail.idx % 8] = 0;
    m->barrier(); ++avail.idx; m->barrier(); regs[0x50/4] = 0;
    while (used.idx == before) {}
    m->barrier();
    return status_byte == 0;
}
static int blocks(int write, unsigned sector, u8 *data, u32 bytes) {
    while (bytes) {
        unsigned n = bytes > 512 ? 512 : bytes;
        if (write) {
            copy(sector_buf, data, n);
            for (unsigned i = n; i < 512; ++i) sector_buf[i] = 0;
        }
        if (!io(write, sector++)) return 0;
        if (!write) copy(data, sector_buf, n);
        data += n; bytes -= n;
    }
    return 1;
}

struct record {
    u64 epoch;
    u32 magic, abi, elf_len, src_len, elf_hash, src_hash, header_hash;
};
static unsigned base(int slot) { return slot * 1024; }
static int header(int slot, struct record *r) {
    if (!blocks(0, base(slot), (u8 *)r, sizeof(*r))) return 0;
    return r->magic == 0x31584b4c && (r->abi >= 1 && r->abi <= ABI_VERSION) && r->epoch &&
        r->elf_len && r->elf_len <= IMAGE_CAP && r->src_len && r->src_len < SOURCE_CAP &&
        r->header_hash == digest(r, (u8 *)&r->header_hash - (u8 *)r);
}
static u8 saved_image[IMAGE_CAP];
static char saved_source[SOURCE_CAP];
static int read_payload(int slot, struct record *r) {
    return blocks(0, base(slot)+1, saved_image, r->elf_len) &&
        blocks(0, base(slot)+513, (u8 *)saved_source, r->src_len) &&
        digest(saved_image, r->elf_len) == r->elf_hash &&
        digest(saved_source, r->src_len) == r->src_hash && m->valid(saved_image, r->elf_len);
}
static int load_record(int slot, struct record *r) {
    return header(slot, r) && read_payload(slot, r);
}
static void select_record(int slot, struct record *r) {
    m->abi = r->abi; m->slot = slot; m->epoch = r->epoch;
    m->source_len = r->src_len; m->image_len = r->elf_len;
    copy(m->image, saved_image, r->elf_len);
    copy((u8 *)m->source, (u8 *)saved_source, r->src_len);
    m->source[r->src_len] = 0;
    m->enter(m);
}
static int publish_header(int slot, struct record *r) {
    r->header_hash = digest(r, (u8 *)&r->header_hash - (u8 *)r);
    return blocks(1, base(slot), (u8 *)r, sizeof(*r)) && io(4, 0);
}
static int stage(int slot, struct record *out) {
    struct record r;
    r.epoch = m->epoch + 1; r.magic = 0x31584b4c; r.abi = ABI_VERSION;
    r.elf_len = m->image_len; r.src_len = m->source_len;
    r.elf_hash = digest(m->image, r.elf_len);
    r.src_hash = digest(m->source, r.src_len);
    if (!blocks(1, base(slot)+1, m->image, r.elf_len) ||
        !blocks(1, base(slot)+513, (u8 *)m->source, r.src_len) || !io(4, 0)) return 0;
    *out = r;
    return read_payload(slot, &r);
}
static int install(int slot) {
    struct record r;
    return stage(slot, &r) && publish_header(slot, &r);
}
static void banner(void) {
    say("Kernel 1.3.0 build="); number(BUILD_NUMBER);
    say(" slot="); put(m->slot < 0 ? '-' : 'A'+m->slot);
    say(" hot="); number(m->switches);
    say(" message=hello-kernel\n");
}
static int replace(char *expr) {
    char *with = expr;
    while (*with && *with != '|') ++with;
    if (!*with || with == expr) return 0;
    *with++ = 0;
    unsigned old = length(expr), fresh = length(with);
    for (unsigned i = 0; i + old <= m->source_len; ++i) {
        if (!prefix(m->source+i, expr)) continue;
        unsigned total = m->source_len - old + fresh;
        if (total >= SOURCE_CAP) return 0;
        if (fresh > old) {
            for (unsigned j = total; j > i+fresh; --j)
                m->source[j-1] = m->source[j-1-(fresh-old)];
        } else {
            for (unsigned j = i+fresh; j < total; ++j)
                m->source[j] = m->source[j+old-fresh];
        }
        copy((u8 *)m->source+i, (u8 *)with, fresh);
        m->source[total] = 0; m->source_len = total;
        return 1;
    }
    return 0;
}
/* Two independent resident banks start after the original 1 MiB disk. */
struct system_record { u32 magic, volume, revision, size, hash, guard; };
static unsigned system_sector(unsigned bank) { return 2048+bank*16384; }
static int system_header(unsigned bank,struct system_record *h) {
    return regs[0x100/4]>=system_sector(bank)+1 &&
        blocks(0,system_sector(bank),(u8 *)h,sizeof *h) &&
        h->magic==0x53595332 && h->volume==m->volume_id &&
        h->revision && (h->revision&1)==bank && h->size && h->size<=SYSTEM_CAP &&
        h->guard==digest(h,20);
}
static int system_latest(struct system_record h[2],int valid[2]) {
    for (unsigned i=0;i<2;++i) valid[i]=system_header(i,h+i);
    return valid[1] && (!valid[0] || h[1].revision>h[0].revision);
}
static int system_read(unsigned bank,struct system_record *h) {
    return blocks(0,system_sector(bank)+1,m->system_buffer,h->size) &&
        digest(m->system_buffer,h->size)==h->hash;
}
static void try_system(void) {
    struct system_record h[2]; int valid[2];
    unsigned newest=system_latest(h,valid);
    for (unsigned n=0;n<2;++n) {
        unsigned bank=newest^n;
        if (valid[bank] && system_read(bank,h+bank)) m->boot_system(m->system_buffer,h[bank].size);
    }
}
static int build_system_disk(int recovery) {
    struct system_record previous[2],h; int valid[2];
    unsigned latest=system_latest(previous,valid);
    if (valid[latest] && !system_read(latest,previous+latest)) {
        valid[latest]=0; latest^=1;
    }
    h.revision=(valid[latest]?previous[latest].revision:0)+1;
    unsigned bank=h.revision&1, sector=system_sector(bank);
    if (recovery) sector=34816; /* separate export slot after the 17 MiB work area */
    if (!h.revision || regs[0x100/4] < sector+1+SYSTEM_CAP/512) {
        say(recovery?"recovery build requires a 25 MiB work disk\n":"system build requires a 17 MiB work disk\n"); return 0;
    }
    const u8 *image;
    if (!m->build_system(&image,&h.size,recovery)) { say("TCC: system build failed\n"); return 0; }
    h.magic=0x53595332; h.volume=m->volume_id; h.hash=digest(image,h.size); h.guard=digest(&h,20);
    if (!blocks(1,sector+1,(u8 *)image,h.size) || !io(4,0) ||
        !blocks(0,sector+1,m->system_buffer,h.size) || digest(m->system_buffer,h.size)!=h.hash ||
        !blocks(1,sector,(u8 *)&h,sizeof h) || !io(4,0)) { say("system ELF save failed\n"); return 0; }
    say(recovery?"TCC: recovery ELF saved, bytes=":"TCC: bootable system ELF saved, bytes="); number(h.size); put('\n'); return 1;
}
/* Each slot reserves its last 64 sectors for an independent tool source bank. */
struct tool_stamp { u32 magic, revision, size, hash, guard; };
static char tool_draft[16384];
static u32 tool_revision;
static int tool_disk, tool_compiled, tool_dirty, app_loaded;
static char app_path[192];
static unsigned tool_bank(unsigned bank) { return base(bank)+960; }
static int tool_header(unsigned bank, struct tool_stamp *h) {
    return blocks(0,tool_bank(bank),(u8 *)h,sizeof *h) && (h->magic==0x544f4f4c || h->magic==0x544f4f32) &&
        h->revision && h->revision%2==bank && h->size && h->size<sizeof tool_draft &&
        h->guard==digest(h,16);
}
static void tools_init(int disk) {
    struct tool_stamp h[2];
    int valid[2]={0,0};
    tool_disk=disk;
    if (disk) for (unsigned i=0;i<2;++i) valid[i]=tool_header(i,&h[i]);
    unsigned newest=valid[1] && (!valid[0] || h[1].revision>h[0].revision);
    for (unsigned n=0;n<2;++n) {
        unsigned i=newest^n;
        if (valid[i] && blocks(0,tool_bank(i)+1,(u8 *)tool_draft,h[i].size) &&
            digest(tool_draft,h[i].size)==h[i].hash) {
            tool_revision=h[i].revision; tool_draft[h[i].size]=0;
            if (h[i].magic==0x544f4f32) return;
            say("older tools preserved on disk; tool previous reads their source\n");
            break;
        }
    }
    unsigned size=length(m->tool_seed);
    if (size<sizeof tool_draft) copy((u8 *)tool_draft,(const u8 *)m->tool_seed,size+1);
}
static int save_tools(void) {
    struct tool_stamp h;
    h.magic=0x544f4f32; h.revision=tool_revision+1; h.size=length(tool_draft);
    h.hash=digest(tool_draft,h.size); h.guard=digest(&h,16);
    unsigned b=tool_bank(h.revision%2);
    if (!tool_disk || !h.revision || !blocks(1,b+1,(u8 *)tool_draft,h.size) || !io(4,0) ||
        !blocks(1,b,(u8 *)&h,sizeof h) || !io(4,0)) return 0;
    tool_revision=h.revision;
    return 1;
}
static int named(const char *cmd,const char *name) {
    unsigned n=length(name); return prefix(cmd,name) && (!cmd[n] || cmd[n]==' ');
}
static void run_tool(const char *cmd) {
    if (tool_dirty) { say("compile tool edits with tcc tools first\n"); return; }
    if (!tool_compiled) { app_loaded=0; tool_compiled=m->tool_compile(tool_draft); }
    if (!tool_compiled) { say("TCC: tool build failed\n"); return; }
    const char *text; unsigned size; int cwd;
    int result=m->tool_run(cmd,m->source,tool_draft,m->cwd,&text,&size,&cwd);
    if (result) {
        if (result==-3) say("user output too large\n");
        else say(result==-2?"user program timed out\n":"user program fault\n");
        return;
    }
    if (named(cmd,"cd")) m->cwd=cwd;
    for (unsigned i=0;i<size;++i) put(text[i]);
}
/* Pure console queries also form the behavioral acceptance interface. */
static int query_command(const char *cmd) {
    if (same(cmd,"version")) banner();
    else if (named(cmd,"pwd") || named(cmd,"ls") || named(cmd,"read") || named(cmd,"cat") || named(cmd,"grep")) run_tool(cmd);
    else if (same(cmd,"note")) { say("note="); say(m->note); put('\n'); }
    else return 0;
    return 1;
}
#include "harness.c"
void _start(struct machine *state) {
    m = state;
    if (m->abi != ABI_VERSION) { say("unsupported handoff\n"); for (;;) {} }
    int disk = disk_init();
    if (!disk) say("disk unavailable\n");
    if (disk && m->slot == -1 && !m->guest_resident && !m->upgrade) try_system();
    if (disk && m->slot == -1) {
        struct record a, b, r;
        int va = header(0, &a), vb = header(1, &b);
        int first = vb && (!va || b.epoch > a.epoch) ? 1 : 0;
        for (int attempt=0; attempt<2; ++attempt) {
            int current=attempt ? 1-first : first;
            if (!load_record(current,&r)) continue;
            if (m->upgrade && r.abi < ABI_VERSION) {
                m->epoch = r.epoch; m->upgrade = 0;
                if (install(1-current) && load_record(1-current,&r)) select_record(1-current,&r);
                if (!load_record(current,&r)) { say("upgrade recovery failed\n"); for (;;) {} }
            }
            select_record(current, &r);
        }
        /* Give the first self-build a bootable predecessor for rollback. */
        if (install(0)) { m->slot = 0; m->epoch = 1; }
    }
    tools_init(disk);
    banner();
    if (disk) agent_boot();
    int ready = 0;
    char cmd[2048];
    for (;;) {
        say("kernel> ");
        if (!input_line(cmd, sizeof cmd, 1)) { say("line too long\n"); continue; }
        if (query_command(cmd)) continue;
        else if (same(cmd,"agent") || prefix(cmd,"agent ")) {
            ready = 0; if (disk) agent_command(cmd); else say("agent needs a disk\n");
        }
        else if (named(cmd,"cd")) run_tool(cmd);
        else if (same(cmd,"tool source")) say(tool_draft);
        else if (same(cmd,"tool previous")) {
            struct tool_stamp h; int found=0;
            for (unsigned n=0;disk && n<2 && !found;++n) {
                unsigned i=(tool_revision&1)^n;
                if (tool_header(i,&h) && h.magic==0x544f4f4c &&
                    blocks(0,tool_bank(i)+1,(u8 *)saved_source,h.size) && digest(saved_source,h.size)==h.hash) {
                    saved_source[h.size]=0; say(saved_source); found=1;
                }
            }
            if (!found) say("no previous tool source\n");
        }
        else if (prefix(cmd,"tool edit ")) {
            char *expr=cmd+10, *with=expr;
            while (*with && *with!='|') ++with;
            if (!*with || with==expr) { say("tool edit OLD|NEW\n"); continue; }
            *with++=0;
            unsigned n=length(tool_draft), old=length(expr), fresh=length(with);
            char *at=tool_draft;
            while (*at && !prefix(at,expr)) ++at;
            if (!*at || n-old+fresh>=sizeof tool_draft) { say("tool edit failed\n"); continue; }
            memmove(at+fresh,at+old,length(at+old)+1);
            copy((u8 *)at,(const u8 *)with,fresh);
            tool_dirty=1; say("tool draft changed\n");
        } else if (same(cmd,"tcc tools")) {
            app_loaded=0; tool_compiled=m->tool_compile(tool_draft);
            if (!tool_compiled) say("TCC: tool build failed\n");
            else if (tool_dirty && !save_tools()) say("tool save failed\n");
            else { tool_dirty=0; say("TCC: EL0 tools ELF ready\n"); }
        } else if (same(cmd,"tcc system") || same(cmd,"tcc all") || same(cmd,"tcc recovery")) {
            if (!disk) { say("system build requires a work disk\n"); continue; }
            if (same(cmd,"tcc all")) {
                tool_compiled=app_loaded=0;
                if (!m->compile_apps(tool_draft)) { say("TCC: application build failed\n"); continue; }
                ready=m->compile(m,BUILD_NUMBER+1);
                if (!ready) { say("TCC: kernel build failed\n"); continue; }
                say("TCC: applications and kernel ready\n");
            }
            build_system_disk(same(cmd,"tcc recovery"));
        } else if (prefix(cmd,"tcc /src/") && !same(cmd,"tcc /src/kernel.c")) {
            tool_compiled=0;
            app_loaded=length(cmd+4)<sizeof app_path && m->compile_app(cmd+4);
            if (app_loaded) copy((u8 *)app_path,(u8 *)cmd+4,length(cmd+4)+1);
            else app_path[0]=0;
            say(app_loaded?"TCC: application ELF ready\n":"TCC: application build failed\n");
        } else if (same(cmd,"run") || prefix(cmd,"run ")) {
            const char *text; unsigned size; int cwd;
            if (!app_path[0]) { say("compile an application first\n"); continue; }
            if (!app_loaded) { tool_compiled=0; app_loaded=m->compile_app(app_path); }
            if (!app_loaded || m->tool_run(cmd[3]?cmd+4:"",m->source,tool_draft,m->cwd,&text,&size,&cwd)) say("application failed\n");
            else for (unsigned i=0;i<size;++i) put(text[i]);
        } else if (same(cmd,"tool reset")) {
            tool_revision=0; tool_dirty=0; tool_compiled=0; tools_init(disk);
            say("tool draft reloaded\n");
        }
        else if (same(cmd,"source")) say(m->source);
        else if (prefix(cmd,"replace ")) {
            if (replace(cmd+8)) { ready = 0; say("source changed\n"); }
            else say("replace failed\n");
        } else if (prefix(cmd,"note ") && length(cmd+5) < sizeof m->note) {
            copy((u8 *)m->note, (u8 *)cmd+5, length(cmd+5)+1);
        } else if (same(cmd,"build") || same(cmd,"tcc kernel") || same(cmd,"tcc /src/kernel.c")) {
            ready = m->compile(m, BUILD_NUMBER + 1);
            say(ready ? "TCC: ELF ready\n" : "TCC: build failed\n");
        } else if (same(cmd,"switch")) {
            int target = m->slot == 0 ? 1 : 0;
            struct record r;
            if (disk && ready && install(target) && load_record(target, &r)) {
                ++m->switches; select_record(target, &r);
            }
            say("switch refused\n");
        } else if (same(cmd,"rollback")) {
            struct record r;
            int other = 1 - m->slot;
            if (disk && m->slot >= 0 && load_record(other, &r)) {
                r.epoch = m->epoch + 1;
                if (publish_header(other, &r)) { ++m->switches; select_record(other, &r); }
            }
            say("rollback unavailable\n");
        } else if (same(cmd,"off")) {
            say("power off\n"); m->poweroff();
        } else say("version | source | replace OLD|NEW | build | switch | rollback | note [TEXT] | pwd | cd | ls | cat | read | grep | tcc tools | tcc kernel | tcc all | tcc system | tcc recovery | tcc /src/FILE.c | run [ARGS] | tool source | tool edit OLD|NEW | tool reset | agent | off\n");
    }
}
