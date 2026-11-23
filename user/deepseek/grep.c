#include "ds_api.h"

static int grepparse(int argc, char **argv, int *opt, const char **pat, char **files, int *nfiles)
{
    int i = 1, end = 0;
    *opt = 0; *pat = 0; *nfiles = 0;
    while (i < argc) {
        const char *a = argv[i];
        if (!end && a[0] == '-' && a[1] != 0) {
            const char *q = a + 1;
            if (ds_eq(a, "--")) { end = 1; i++; continue; }
            while (*q) {
                if (*q == 'i') *opt |= 1;
                else if (*q == 'n') *opt |= 2;
                else if (*q == 'v') *opt |= 4;
                else if (*q == 'F') *opt |= 8;
                else if (*q == 'c') *opt |= 16;
                else return ds_fail("invalid option");
                q++;
            }
            i++;
        } else break;
    }
    if (i >= argc) return ds_fail("missing pattern");
    *pat = argv[i++];
    while (i < argc) { files[(*nfiles)++] = argv[i++]; }
    return 0;
}

static int countlines(DS d)
{
    unsigned pos = 0; DS line; int c = 0;
    while (ds_line(d, &pos, &line)) c++;
    return c;
}

static int dogrep(DS data, const char *pat, int opt)
{
    unsigned pos = 0; DS line; int count = 0, matched = 0;
    int fixed = opt & 8, insens = opt & 1, inv = opt & 4, num = opt & 2;
    while (ds_line(data, &pos, &line)) {
        count++;
        int m;
        if (fixed) {
            unsigned len = ds_len(pat);
            if (len == 0) m = 1;
            else {
                m = 0;
                unsigned i;
                for (i = 0; i + len <= line.n; i++) {
                    if (ds_match((DS){line.p + i, len}, pat, insens)) { m = 1; break; }
                }
            }
        }
        else m = ds_match(line, pat, insens);
        if (inv) m = !m;
        if (m) {
            matched++;
            if (!(opt & 16)) {
                if (num) {
                    char buf[24]; int v = count, j = 0, t;
                    if (v == 0) buf[j++] = '0';
                    else { char r[24]; t = 0;
                        while (v > 0) { r[t++] = (char)('0' + v % 10); v /= 10; }
                        while (t > 0) buf[j++] = r[--t]; }
                    buf[j++] = ':';
                    ds_emit(buf, (unsigned)j);
                }
                if (ds_emit(line.p, line.n) != 0) return 1;
                if (ds_emit("\n", 1) != 0) return 1;
            }
        }
    }
    if (opt & 16) {
        ds_number(matched);
        ds_emit("\n", 1);
    }
    return matched > 0 ? 0 : 1;
}

int ds_grep(int argc, char **argv, DS input)
{
    char *files[256];
    int nfiles = 0, opt = 0;
    const char *pat = 0;
    int r = grepparse(argc, argv, &opt, &pat, files, &nfiles);
    if (r) return r;
    if (nfiles == 0) {
        DS data;
        if (ds_input(0, input, &data) != 0) return ds_fail("cannot read input");
        return dogrep(data, pat, opt);
    }
    int any = 0;
    int i;
    for (i = 0; i < nfiles; i++) {
        DS data;
        if (ds_input(files[i], input, &data) != 0) return ds_fail("cannot read file");
        int n = countlines(data);
        (void)n;
        int rr = dogrep(data, pat, opt);
        if (rr == 0) any = 1;
        else if (rr != 1) return 1;
    }
    return any ? 0 : 1;
}
