#include "ds_api.h"

static int emit_str(const char *s)
{
    unsigned n = ds_len(s);
    if (ds_emit(s, n) != 0) return 1;
    return 0;
}

static int emit_nl(void)
{
    if (ds_emit("\n", 1) != 0) return 1;
    return 0;
}

/* Count length of a DS-like path string (not NUL terminated) */
static unsigned str_n(const char *s, unsigned max)
{
    unsigned i = 0;
    while (i < max && s[i]) i++;
    return i;
}

/* basename: strip trailing slashes (except if result empty), find last segment */
static int do_basename(const char *path, unsigned plen, const char *suffix, unsigned slen)
{
    unsigned end = plen;
    unsigned start;
    unsigned seglen;
    while (end > 0 && path[end - 1] == '/') end--;
    if (end == 0) {
        /* path was all slashes or empty */
        if (plen == 0) {
            if (ds_emit("\n", 1) != 0) return 1;
            return 0;
        }
        if (ds_emit("/", 1) != 0) return 1;
        if (ds_emit("\n", 1) != 0) return 1;
        return 0;
    }
    start = end;
    while (start > 0 && path[start - 1] != '/') start--;
    seglen = end - start;
    /* strip suffix if present and not equal to whole segment */
    if (suffix && slen > 0 && seglen > slen) {
        unsigned i;
        int match = 1;
        for (i = 0; i < slen; i++) {
            if (path[start + seglen - slen + i] != suffix[i]) { match = 0; break; }
        }
        if (match) seglen -= slen;
    }
    if (ds_emit(path + start, seglen) != 0) return 1;
    if (ds_emit("\n", 1) != 0) return 1;
    return 0;
}

/* dirname: lexical, strip trailing slashes except root */
static int do_dirname(const char *path, unsigned plen)
{
    unsigned end = plen;
    unsigned i;
    while (end > 1 && path[end - 1] == '/') end--;
    if (end == 0) {
        if (ds_emit(".", 1) != 0) return 1;
        if (ds_emit("\n", 1) != 0) return 1;
        return 0;
    }
    if (end == 1 && path[0] == '/') {
        if (ds_emit("/", 1) != 0) return 1;
        if (ds_emit("\n", 1) != 0) return 1;
        return 0;
    }
    /* find last slash within [0,end) */
    i = end;
    while (i > 0 && path[i - 1] != '/') i--;
    if (i == 0) {
        /* no slash at all */
        if (ds_emit(".", 1) != 0) return 1;
        if (ds_emit("\n", 1) != 0) return 1;
        return 0;
    }
    /* i is index after the last slash; dir is path[0..i-1) but strip trailing slashes */
    i--;
    while (i > 0 && path[i - 1] == '/') i--;
    if (i == 0) {
        if (ds_emit("/", 1) != 0) return 1;
        if (ds_emit("\n", 1) != 0) return 1;
        return 0;
    }
    if (ds_emit(path, i) != 0) return 1;
    if (ds_emit("\n", 1) != 0) return 1;
    return 0;
}

int ds_paths(int argc, char **argv, DS input)
{
    unsigned len;
    (void)input;
    if (argc < 1) return ds_fail("missing command");
    len = ds_len(argv[0]);
    (void)len;

    if (ds_match((DS){argv[0], ds_len(argv[0])}, "basename", 0)) {
        if (argc < 2) return ds_fail("basename: missing operand");
        if (argc > 3) return ds_fail("basename: too many operands");
        {
            const char *path = argv[1];
            unsigned plen = str_n(path, 4096);
            const char *sfx = NULL;
            unsigned slen = 0;
            if (argc == 3) { sfx = argv[2]; slen = str_n(sfx, 4096); }
            return do_basename(path, plen, sfx, slen);
        }
    }

    if (ds_match((DS){argv[0], ds_len(argv[0])}, "dirname", 0)) {
        if (argc < 2) return ds_fail("dirname: missing operand");
        if (argc > 2) return ds_fail("dirname: too many operands");
        {
            const char *path = argv[1];
            unsigned plen = str_n(path, 4096);
            return do_dirname(path, plen);
        }
    }

    if (ds_match((DS){argv[0], ds_len(argv[0])}, "pwd", 0)) {
        if (argc > 1) return ds_fail("pwd: too many operands");
        {
            const char *cwd = ds_cwd();
            if (!cwd) return ds_fail("pwd: cannot determine cwd");
            if (emit_str(cwd) != 0) return 1;
            if (emit_nl() != 0) return 1;
            return 0;
        }
    }

    if (ds_match((DS){argv[0], ds_len(argv[0])}, "cd", 0)) {
        if (argc > 2) return ds_fail("cd: too many operands");
        {
            const char *dir = "/";
            if (argc == 2) dir = argv[1];
            if (ds_setcwd(dir) != 0) return ds_fail("cd: cannot change directory");
            return 0;
        }
    }

    return ds_fail("paths: unknown command");
}
