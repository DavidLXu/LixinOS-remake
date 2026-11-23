#include "ds_api.h"

static int emit_num_sep(int first, int step, int last, int started, char *sep)
{
    int i;
    for (i = first; step > 0 ? i <= last : i >= last; i += step) {
        if (started) {
            if (ds_emit(sep, 1u) != 0) return 1;
        }
        started = 1;
        {
            char buf[32];
            int n = 0, neg = 0;
            unsigned u;
            if (i < 0) { neg = 1; u = (unsigned)(-(i + 1)) + 1u; }
            else u = (unsigned)i;
            {
                char tmp[16];
                int t = 0;
                if (u == 0u) tmp[t++] = '0';
                while (u > 0u) { tmp[t++] = (char)('0' + (int)(u % 10u)); u /= 10u; }
                if (neg) buf[n++] = '-';
                while (t > 0) buf[n++] = tmp[--t];
            }
            if (ds_emit(buf, (unsigned)n) != 0) return 1;
        }
    }
    return 0;
}

static int do_echo(int argc, char **argv)
{
    int i = 1, newline = 1, first = 1;
    if (i < argc && ds_eq(argv[i], "-n")) { newline = 0; i++; }
    for (; i < argc; i++) {
        if (!first) {
            if (ds_emit(" ", 1u) != 0) return ds_fail("echo: write error");
        }
        first = 0;
        {
            unsigned n = ds_len(argv[i]);
            if (n > 0u && ds_emit(argv[i], n) != 0) return ds_fail("echo: write error");
        }
    }
    if (newline) {
        if (ds_emit("\n", 1u) != 0) return ds_fail("echo: write error");
    }
    return 0;
}

static int do_seq(int argc, char **argv)
{
    int vals[3];
    int count = argc - 1;
    int i, first, step, last;
    if (count < 1 || count > 3) return ds_fail("seq: expected 1 to 3 integers");
    for (i = 0; i < count; i++) {
        if (!ds_num(argv[i + 1], &vals[i])) return ds_fail("seq: invalid integer");
    }
    if (count == 1) { first = 1; step = 1; last = vals[0]; }
    else if (count == 2) { first = vals[0]; step = 1; last = vals[1]; }
    else { first = vals[0]; step = vals[1]; last = vals[2]; }
    if (step == 0) return ds_fail("seq: step must not be zero");
    if (emit_num_sep(first, step, last, 0, "\n") != 0) return ds_fail("seq: write error");
    if ((step > 0 && first <= last) || (step < 0 && first >= last)) {
        if (ds_emit("\n", 1u) != 0) return ds_fail("seq: write error");
    }
    return 0;
}

int ds_basic(int argc, char **argv, DS input)
{
    (void)input;
    if (argc < 1 || argv == 0) return ds_fail("missing command");
    if (ds_eq(argv[0], "echo")) return do_echo(argc, argv);
    if (ds_eq(argv[0], "true")) {
        if (argc != 1) return ds_fail("true: takes no arguments");
        return 0;
    }
    if (ds_eq(argv[0], "false")) {
        if (argc != 1) return ds_fail("false: takes no arguments");
        return 1;
    }
    if (ds_eq(argv[0], "seq")) return do_seq(argc, argv);
    return ds_fail("unknown command");
}
