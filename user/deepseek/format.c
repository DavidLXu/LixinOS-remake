#include "ds_api.h"

static unsigned fmt_scan(const char *f, unsigned n, unsigned *i, char *out, unsigned cap, DS *args, int nargs, int *used)
{
    unsigned o = 0;
    while (*i < n) {
        char c = f[*i];
        if (c == 92) {
            if (*i + 1 >= n) {
                *used = -1;
                return 0;
            }
            char d = f[*i + 1];
            if (d == 110) { out[o++] = 10; *i += 2; }
            else if (d == 116) { out[o++] = 9; *i += 2; }
            else if (d == 92) { out[o++] = 92; *i += 2; }
            else {
                *used = -2;
                return 0;
            }
        } else if (c == 37) {
            if (*i + 1 >= n) {
                *used = -3;
                return 0;
            }
            char d = f[*i + 1];
            if (d == 37) { out[o++] = 37; *i += 2; }
            else if (d == 115) {
                DS s = { "", 0 };
                if (*used < nargs) { s = args[*used]; (*used)++; }
                for (unsigned k = 0; k < s.n && o < cap; k++) out[o++] = s.p[k];
                *i += 2;
            } else {
                *used = -4;
                return 0;
            }
        } else {
            out[o++] = c;
            *i += 1;
        }
        if (o >= cap) break;
    }
    return o;
}

static int fmt_consumes(const char *f, unsigned n)
{
    for (unsigned i = 0; i + 1 < n; i++) {
        if (f[i] == 92) { i++; continue; }
        if (f[i] == 37) {
            if (f[i + 1] == 115) return 1;
            if (f[i + 1] == 37) { i++; continue; }
            return 0;
        }
    }
    return 0;
}

int ds_format(int argc, char **argv, DS input)
{
    (void)input;
    if (argc < 2) return ds_fail("usage: printf FORMAT [ARG...]");
    const char *f = argv[1];
    unsigned n = ds_len(f);

    /* validate format */
    for (unsigned i = 0; i < n; i++) {
        if (f[i] == 92) {
            if (i + 1 >= n) return ds_fail("bad backslash escape");
            char d = f[i + 1];
            if (!(d == 110 || d == 116 || d == 92)) return ds_fail("unsupported escape");
            i++;
        } else if (f[i] == 37) {
            if (i + 1 >= n) return ds_fail("incomplete % directive");
            char d = f[i + 1];
            if (!(d == 115 || d == 37)) return ds_fail("unsupported format directive");
            i++;
        }
    }

    DS args[256];
    int nargs = argc - 2;
    if (nargs > 256) nargs = 256;
    for (int k = 0; k < nargs; k++) {
        const char *a = argv[k + 2];
        DS s = { a, ds_len(a) };
        args[k] = s;
    }

    char buf[4096];
    unsigned i = 0;
    int used = 0;
    int consumes = fmt_consumes(f, n);

    for (;;) {
        unsigned idx = i;
        int start = used;
        unsigned o = fmt_scan(f, n, &idx, buf, sizeof buf, args, nargs, &used);
        if (used == -1) return ds_fail("bad backslash escape");
        if (used == -2) return ds_fail("unsupported escape");
        if (used == -3) return ds_fail("incomplete % directive");
        if (used == -4) return ds_fail("unsupported format directive");
        if (ds_emit(buf, o)) return ds_fail("emit failed");
        i = idx;
        if (used >= nargs) break;
        if (!consumes) break;
        if (used == start) break;
        if (i >= n) i = 0;
    }
    return 0;
}
