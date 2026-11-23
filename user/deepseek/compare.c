#include "ds_api.h"

#define MAXLINES 8192

static unsigned off_old[MAXLINES + 1];
static unsigned off_new[MAXLINES + 1];

/* Build offsets into d: off[0]=0, off[k]=offset past k-th line.
   ds_line consumes newline if present. Return line count or -1. */
static int split_lines(DS d, unsigned *off, int max) {
    unsigned pos = 0;
    DS line;
    int n = 0;
    off[0] = 0;
    if (d.n == 0) return 0;
    while (ds_line(d, &pos, &line)) {
        if (n >= max) return -1;
        n++;
        off[n] = pos;
    }
    return n;
}

static unsigned lstart(const unsigned *off, int i) { return off[i]; }
static unsigned lend(const unsigned *off, DS d, int i) {
    unsigned e = off[i + 1];
    if (e > 0 && e <= d.n && d.p[e - 1] == '\n') e--;
    return e;
}
static unsigned llen(const unsigned *off, DS d, int i) {
    return lend(off, d, i) - lstart(off, i);
}

static int line_eq(const unsigned *ao, DS a, int i, const unsigned *bo, DS b, int j) {
    unsigned la = llen(ao, a, i), lb = llen(bo, b, j);
    unsigned pa = lstart(ao, i), pb = lstart(bo, j);
    unsigned k;
    if (la != lb) return 0;
    for (k = 0; k < la; k++) if (a.p[pa + k] != b.p[pb + k]) return 0;
    return 1;
}

static int emit_bytes(const char *p, unsigned n) { return ds_emit(p, n); }

static int emit_line(char prefix, const unsigned *off, DS d, int i) {
    unsigned s = lstart(off, i), e = lend(off, d, i);
    if (ds_emit(&prefix, 1)) return 1;
    if (e > s && ds_emit(d.p + s, e - s)) return 1;
    if (ds_emit("\n", 1)) return 1;
    return 0;
}

static int emit_noeol(void) {
    const char *msg = "\\ No newline at end of file\n";
    unsigned n = 0;
    while (msg[n]) n++;
    return ds_emit(msg, n);
}

static int emit_num(int v) {
    char tmp[16];
    unsigned tn = 0, n = 0, i;
    char buf[16];
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) tmp[tn++] = '0';
    while (v > 0) { tmp[tn++] = (char)('0' + v % 10); v /= 10; }
    if (neg) buf[n++] = '-';
    for (i = 0; i < tn; i++) buf[n++] = tmp[tn - 1 - i];
    return ds_emit(buf, n);
}

static int emit_range(int start, int count) {
    if (emit_num(start)) return 1;
    if (ds_emit(",", 1)) return 1;
    if (emit_num(count)) return 1;
    return 0;
}

static int last_noeol(DS d, int n) {
    if (n <= 0) return 0;
    if (d.n == 0) return 0;
    return d.p[d.n - 1] != '\n';
}

int ds_compare(int argc, char **argv, DS input) {
    int argi = 1;
    DS oldd, newd;
    const char *oldname, *newname;
    int nold, nnew, pre, suf, ocount, ncount, i;
    int start_old, start_new;

    (void)input;

    if (argc >= 2 && ds_eq(argv[1], "-u")) argi = 2;
    if (argc - argi != 2) return ds_fail("usage: diff [-u] OLD NEW");
    oldname = argv[argi];
    newname = argv[argi + 1];

    if (ds_input(oldname, input, &oldd)) return ds_fail("cannot read OLD");
    if (ds_input(newname, input, &newd)) return ds_fail("cannot read NEW");

    nold = split_lines(oldd, off_old, MAXLINES);
    if (nold < 0) return ds_fail("too many lines in OLD");
    nnew = split_lines(newd, off_new, MAXLINES);
    if (nnew < 0) return ds_fail("too many lines in NEW");

    pre = 0;
    while (pre < nold && pre < nnew && line_eq(off_old, oldd, pre, off_new, newd, pre)) pre++;

    suf = 0;
    while (suf < nold - pre && suf < nnew - pre &&
           line_eq(off_old, oldd, nold - 1 - suf, off_new, newd, nnew - 1 - suf)) suf++;

    ocount = nold - pre - suf;
    ncount = nnew - pre - suf;

    if (ocount == 0 && ncount == 0) return 0;

    /* Always emit unified headers. */
    if (ds_emit("--- ", 4)) return 2;
    if (ds_emit(oldname, ds_len(oldname))) return 2;
    if (ds_emit("\n+++ ", 5)) return 2;
    if (ds_emit(newname, ds_len(newname))) return 2;
    if (ds_emit("\n", 1)) return 2;

    start_old = (ocount == 0) ? pre : pre + 1;
    start_new = (ncount == 0) ? pre : pre + 1;

    if (ds_emit("@@ -", 4)) return 2;
    if (emit_range(start_old, ocount)) return 2;
    if (ds_emit(" +", 2)) return 2;
    if (emit_range(start_new, ncount)) return 2;
    if (ds_emit(" @@\n", 4)) return 2;

    for (i = 0; i < ocount; i++) {
        int idx = pre + i;
        if (emit_line('-', off_old, oldd, idx)) return 2;
        if (idx == nold - 1 && last_noeol(oldd, nold)) {
            if (emit_noeol()) return 2;
        }
    }
    for (i = 0; i < ncount; i++) {
        int idx = pre + i;
        if (emit_line('+', off_new, newd, idx)) return 2;
        if (idx == nnew - 1 && last_noeol(newd, nnew)) {
            if (emit_noeol()) return 2;
        }
    }

    (void)emit_bytes;
    return 1;
}
