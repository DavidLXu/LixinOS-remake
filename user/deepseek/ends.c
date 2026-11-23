#include "ds_api.h"

static int parse_n(int argc, char **argv, int *n) {
    int i = 1;
    *n = 10;
    while (i < argc) {
        if (ds_eq(argv[i], "-n")) {
            if (i + 1 >= argc) return 0;
            if (!ds_num(argv[i + 1], n)) return 0;
            if (*n < 0) return 0;
            i += 2;
        } else if (argv[i][0] == '-' && !ds_eq(argv[i], "-")) {
            return 0;
        } else {
            break;
        }
    }
    return 1;
}

static int get_input(int argc, char **argv, int start, DS input, DS *data) {
    const char *name = "-";
    int nonflag = 0;
    int i;
    for (i = start; i < argc; i++) {
        if (argv[i][0] != '-' || ds_eq(argv[i], "-")) {
            name = argv[i];
            nonflag++;
        } else {
            return -1;
        }
    }
    if (nonflag > 1) return -1;
    if (ds_input(name, input, data) != 0) return -1;
    return 0;
}

static int do_head(int argc, char **argv, DS input) {
    int n, printed = 0;
    unsigned pos = 0;
    DS line, data;
    int start = 1;
    if (!parse_n(argc, argv, &n)) return ds_fail("head: invalid -n");
    while (start < argc && ds_eq(argv[start], "-n")) start += 2;
    if (get_input(argc, argv, start, input, &data) != 0)
        return ds_fail("head: invalid arguments");
    while (printed < n && ds_line(data, &pos, &line)) {
        ds_emit(line.p, line.n);
        ds_emit("\n", 1);
        printed++;
    }
    /* handle trailing unterminated line */
    if (printed < n && pos < data.n) {
        unsigned rem = data.n - pos;
        ds_emit(data.p + pos, rem);
    }
    return 0;
}

static int do_tail(int argc, char **argv, DS input) {
    int n, total = 0, skip;
    unsigned pos = 0, start = 0;
    DS line, data;
    int startarg = 1;
    if (!parse_n(argc, argv, &n)) return ds_fail("tail: invalid -n");
    while (startarg < argc && ds_eq(argv[startarg], "-n")) startarg += 2;
    if (get_input(argc, argv, startarg, input, &data) != 0)
        return ds_fail("tail: invalid arguments");
    pos = 0;
    while (ds_line(data, &pos, &line)) total++;
    if (pos < data.n) total++;
    skip = total - n;
    if (skip < 0) skip = 0;
    pos = 0;
    while (skip > 0 && pos < data.n) {
        unsigned save = pos;
        if (ds_line(data, &pos, &line)) {
            skip--;
        } else {
            pos = save;
            break;
        }
    }
    if (skip == 0) {
        if (pos < data.n) {
            start = pos;
            ds_emit(data.p + start, data.n - start);
        } else if (pos == data.n && total > 0) {
            /* all lines had trailing newline, nothing left after last LF */
        }
    }
    return 0;
}

static int do_nl(int argc, char **argv, DS input) {
    DS data, line;
    unsigned pos = 0;
    int num = 1, i;
    const char *name = "-";
    int nonflag = 0;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || ds_eq(argv[i], "-")) {
            name = argv[i];
            nonflag++;
        } else {
            return ds_fail("nl: invalid arguments");
        }
    }
    if (nonflag > 1) return ds_fail("nl: invalid arguments");
    if (ds_input(name, input, &data) != 0)
        return ds_fail("nl: cannot read input");
    while (pos < data.n) {
        unsigned before = pos;
        int got = ds_line(data, &pos, &line);
        if (!got) break;
        ds_number(num);
        ds_emit("\t", 1);
        ds_emit(line.p, line.n);
        ds_emit("\n", 1);
        num++;
        if (pos == before) break;
    }
    /* handle final unterminated line (ds_line should return it, but safety) */
    if (pos < data.n) {
        unsigned rem = data.n - pos;
        ds_number(num);
        ds_emit("\t", 1);
        ds_emit(data.p + pos, rem);
        ds_emit("\n", 1);
    }
    return 0;
}

int ds_ends(int argc, char **argv, DS input) {
    if (argc < 1) return ds_fail("usage");
    if (ds_eq(argv[0], "head")) return do_head(argc, argv, input);
    if (ds_eq(argv[0], "tail")) return do_tail(argc, argv, input);
    if (ds_eq(argv[0], "nl")) return do_nl(argc, argv, input);
    return ds_fail("unknown command");
}
