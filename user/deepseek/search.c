#include "ds_api.h"

static int is_under(const char *path, const char *base) {
    unsigned bl = ds_len(base);
    unsigned pl = ds_len(path);
    if (bl == 0) return 1;
    if (bl == 1 && base[0] == '/') {
        return path[0] == '/';
    }
    unsigned i;
    for (i = 0; i < bl; i++) {
        if (path[i] != base[i]) return 0;
    }
    if (pl == bl) return 1;
    if (path[bl] == '/') return 1;
    return 0;
}

static int check_line(DS line, const char *text, int insensitive) {
    if (ds_match(line, text, insensitive)) return 1;
    return 0;
}

struct search_ctx {
    const char *text;
    int insensitive;
    int listonly;
    int hits;
    int errflag;
};

static void emit_line(const char *abs, int lineno, DS line) {
    char numbuf[16];
    int v = lineno;
    int n = 0;
    if (v == 0) { numbuf[n++] = '0'; }
    else {
        char tmp[16];
        int t = 0;
        while (v > 0) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
        while (t > 0) numbuf[n++] = tmp[--t];
    }
    ds_emit(abs, ds_len(abs));
    ds_emit(":", 1);
    ds_emit(numbuf, (unsigned)n);
    ds_emit(":", 1);
    if (line.n > 0) ds_emit(line.p, line.n);
    ds_emit("\n", 1);
}

static void visit(const char *absolute, int kind, void *ctxp) {
    struct search_ctx *ctx = (struct search_ctx *)ctxp;
    if (kind != 1) return;
    DS data;
    if (ds_kind(absolute, &data) != 1) return;
    unsigned pos = 0;
    DS line;
    int lineno = 0;
    int matched_file = 0;
    while (ds_line(data, &pos, &line)) {
        lineno++;
        if (check_line(line, ctx->text, ctx->insensitive)) {
            matched_file = 1;
            ctx->hits = 1;
            if (ctx->listonly) break;
            emit_line(absolute, lineno, line);
        }
    }
    if (ctx->listonly && matched_file) {
        ds_emit(absolute, ds_len(absolute));
        ds_emit("\n", 1);
    }
}

struct filter_ctx {
    struct search_ctx inner;
    const char *base;
    const char *text;
    int insensitive;
    int listonly;
    int hits;
};

static void filter_visit(const char *absolute, int kind, void *ctxp) {
    struct filter_ctx *fc = (struct filter_ctx *)ctxp;
    const char *base = fc->base;
    if (!is_under(absolute, base)) return;
    if (kind != 1 && kind != 2) return;
    /* base itself: if file, process; if dir, we only process under */
    if (kind != 1) return;
    DS data;
    if (ds_kind(absolute, &data) != 1) return;
    unsigned pos = 0;
    DS line;
    int lineno = 0;
    int matched_file = 0;
    while (ds_line(data, &pos, &line)) {
        lineno++;
        if (ds_match(line, fc->text, fc->insensitive)) {
            matched_file = 1;
            fc->hits = 1;
            if (fc->listonly) break;
            char numbuf[16];
            int v = lineno;
            int n = 0;
            if (v == 0) { numbuf[n++] = '0'; }
            else {
                char tmp[16]; int t = 0;
                while (v > 0) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
                while (t > 0) numbuf[n++] = tmp[--t];
            }
            ds_emit(absolute, ds_len(absolute));
            ds_emit(":", 1);
            ds_emit(numbuf, (unsigned)n);
            ds_emit(":", 1);
            if (line.n > 0) ds_emit(line.p, line.n);
            ds_emit("\n", 1);
        }
    }
    if (fc->listonly && matched_file) {
        ds_emit(absolute, ds_len(absolute));
        ds_emit("\n", 1);
    }
}

int ds_search(int argc, char **argv, DS input) {
    (void)input;
    int insensitive = 0;
    int listonly = 0;
    int argi = 1;
    while (argi < argc) {
        const char *a = argv[argi];
        if (a[0] == '-' && a[1] != '\0') {
            const char *q = a + 1;
            while (*q) {
                if (*q == 'i') insensitive = 1;
                else if (*q == 'l') listonly = 1;
                else return ds_fail("unknown option");
                q++;
            }
            argi++;
        } else break;
    }
    if (argi >= argc) return ds_fail("missing TEXT");
    const char *text = argv[argi];
    argi++;
    const char *target = ".";
    if (argi < argc) {
        target = argv[argi];
        argi++;
    }
    if (argi < argc) return ds_fail("too many arguments");

    char absbuf[256];
    if (ds_path(target, absbuf) != 0) return ds_fail("invalid path");

    struct filter_ctx fc;
    fc.base = absbuf;
    fc.text = text;
    fc.insensitive = insensitive;
    fc.listonly = listonly;
    fc.hits = 0;

    ds_walk(filter_visit, &fc);

    if (fc.hits) return 0;
    return 1;
}
