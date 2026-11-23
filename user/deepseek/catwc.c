#include "ds_api.h"

static int is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static void count_bytes(DS data, unsigned long long *lines, unsigned long long *words, unsigned long long *bytes) {
    unsigned i;
    int in_word = 0;
    *lines = *words = *bytes = 0;
    for (i = 0; i < data.n; i++) {
        unsigned char c = (unsigned char)data.p[i];
        (*bytes)++;
        if (c == '\n') (*lines)++;
        if (is_space(c)) {
            in_word = 0;
        } else {
            if (!in_word) (*words)++;
            in_word = 1;
        }
    }
}

static int is_flag(const char *s) {
    if (s[0] != '-') return 0;
    if (s[1] == '\0') return 0;
    return 1;
}

static int parse_wc_flags(const char *s, int *want_l, int *want_w, int *want_c) {
    int i;
    for (i = 1; s[i]; i++) {
        if (s[i] == 'l') *want_l = 1;
        else if (s[i] == 'w') *want_w = 1;
        else if (s[i] == 'c') *want_c = 1;
        else return 0;
    }
    return 1;
}

static void print_ull(unsigned long long v) {
    char buf[32];
    int i = 0;
    if (v == 0) {
        ds_put("0");
        return;
    }
    while (v > 0) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        i--;
        ds_emit(&buf[i], 1);
    }
}

static int do_cat(int argc, char **argv, DS input) {
    int i;
    int any = 0;
    if (argc < 2) {
        return ds_emit(input.p ? input.p : "", input.n) == 0 ? 0 : ds_fail("write failed");
    }
    for (i = 1; i < argc; i++) {
        DS data;
        if (ds_input(argv[i], input, &data) != 0) {
            return ds_fail("cannot read input");
        }
        if (ds_emit(data.p, data.n) != 0) {
            return ds_fail("write failed");
        }
        any = 1;
    }
    return any ? 0 : 0;
}

static int do_wc(int argc, char **argv, DS input) {
    int want_l = 0, want_w = 0, want_c = 0;
    int i;
    int have_file = 0;
    const char *file = 0;
    DS data;
    unsigned long long lines, words, bytes;

    for (i = 1; i < argc; i++) {
        if (is_flag(argv[i])) {
            if (!parse_wc_flags(argv[i], &want_l, &want_w, &want_c)) {
                return ds_fail("unknown option");
            }
        } else {
            if (have_file) return ds_fail("too many arguments");
            file = argv[i];
            have_file = 1;
        }
    }

    if (!want_l && !want_w && !want_c) {
        want_l = want_w = want_c = 1;
    }

    if (have_file) {
        if (ds_input(file, input, &data) != 0) {
            return ds_fail("cannot read file");
        }
    } else {
        data = input;
    }

    count_bytes(data, &lines, &words, &bytes);

    {
        int first = 1;
        if (want_l) {
            print_ull(lines);
            first = 0;
        }
        if (want_w) {
            if (!first) ds_emit(" ", 1);
            print_ull(words);
            first = 0;
        }
        if (want_c) {
            if (!first) ds_emit(" ", 1);
            print_ull(bytes);
            first = 0;
        }
    }
    ds_emit("\n", 1);
    return 0;
}

int ds_catwc(int argc, char **argv, DS input) {
    if (argc < 1 || ds_eq(argv[0], "cat")) {
        return do_cat(argc, argv, input);
    }
    if (ds_eq(argv[0], "wc")) {
        return do_wc(argc, argv, input);
    }
    return ds_fail("unknown subcommand");
}
