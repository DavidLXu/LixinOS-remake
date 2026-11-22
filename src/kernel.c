#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif
/* The build embeds abi.h followed by this file as one editable translation unit. */
static struct machine *m;
static void put(char c) {
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
static u32 digest(const void *data, u32 n) {
    const u8 *p = data;
    u32 h = 2166136261U;
    while (n--) h = (h ^ *p++) * 16777619U;
    return h;
}
static int line(char *s, unsigned cap) {
    unsigned n = 0;
    int full = 0;
    for (;;) {
        while (*(volatile u32 *)0x09000018 & 16) {}
        char c = *(volatile u32 *)0x09000000;
        if (c == '\r' || c == '\n') { put('\n'); s[n] = 0; return !full; }
        if (c == 8 || c == 127) {
            if (n) { --n; say("\b \b"); }
        } else if (n + 1 < cap) { s[n++] = c; put(c); }
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
    return r->magic == 0x31584b4c && r->abi == ABI_VERSION && r->epoch &&
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
    m->slot = slot; m->epoch = r->epoch;
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
static int install(int slot) {
    struct record r;
    r.epoch = m->epoch + 1; r.magic = 0x31584b4c; r.abi = ABI_VERSION;
    r.elf_len = m->image_len; r.src_len = m->source_len;
    r.elf_hash = digest(m->image, r.elf_len);
    r.src_hash = digest(m->source, r.src_len);
    if (!blocks(1, base(slot)+1, m->image, r.elf_len) ||
        !blocks(1, base(slot)+513, (u8 *)m->source, r.src_len) || !io(4, 0)) return 0;
    return read_payload(slot, &r) && publish_header(slot, &r);
}
static void banner(void) {
    say("Kernel 1.0.0 build="); number(BUILD_NUMBER);
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
void _start(struct machine *state) {
    m = state;
    if (m->abi != ABI_VERSION) { say("unsupported handoff\n"); for (;;) {} }
    int disk = disk_init();
    if (!disk) say("disk unavailable\n");
    if (disk && m->slot == -1) {
        struct record a, b, r;
        int va = header(0, &a), vb = header(1, &b);
        int first = vb && (!va || b.epoch > a.epoch) ? 1 : 0;
        if (load_record(first, &r)) select_record(first, &r);
        if (load_record(1-first, &r)) select_record(1-first, &r);
        /* Give the first self-build a bootable predecessor for rollback. */
        if (install(0)) { m->slot = 0; m->epoch = 1; }
    }
    banner();
    int ready = 0;
    char cmd[256];
    for (;;) {
        say("kernel> ");
        if (!line(cmd, sizeof cmd)) { say("line too long\n"); continue; }
        if (same(cmd,"version")) banner();
        else if (same(cmd,"source")) say(m->source);
        else if (prefix(cmd,"replace ")) {
            if (replace(cmd+8)) { ready = 0; say("source changed\n"); }
            else say("replace failed\n");
        } else if (prefix(cmd,"note ") && length(cmd+5) < sizeof m->note) {
            copy((u8 *)m->note, (u8 *)cmd+5, length(cmd+5)+1);
        } else if (same(cmd,"note")) { say("note="); say(m->note); put('\n'); }
        else if (same(cmd,"build")) {
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
        } else say("version | source | replace OLD|NEW | build | switch | rollback | note [TEXT] | off\n");
    }
}
