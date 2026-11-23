#include "api.h"
#include "commands.h"
#include "bearssl_hash.h"
#define LIXINOS_WORK_INPUT
#include "sourcefs.h"

void *memcpy(void *dst, const void *src, unsigned long n) {
    char *d = dst;
    const char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}
void *memset(void *dst, int v, unsigned long n) {
    char *d = dst;
    while (n--)
        *d++ = v;
    return dst;
}
void *memmove(void *dst, const void *src, unsigned long n) {
    char *d = dst;
    const char *s = src;
    if (d < s)
        return memcpy(d, s, n);
    while (n) {
        --n;
        d[n] = s[n];
    }
    return dst;
}
static struct tool_input *input;
static struct tool_output *output;
#define CAP 98304
static char buffers[2][CAP];
#include "ds_api.h"
static char *dst;
static unsigned used;
static const char *error;
static char cwd[256];
static unsigned length(const char *p) {
    unsigned n = 0;
    while (p[n])
        ++n;
    return n;
}
static void copy(char *d, const char *s, unsigned n) {
    while (n--)
        *d++ = *s++;
}
static int same(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static int starts(const char *s, const char *p) {
    while (*p && *s == *p) {
        ++s;
        ++p;
    }
    return !*p;
}
static int fail(const char *s) {
    error = s;
    return 2;
}
static int emit(const char *p, unsigned n) {
    if (n > CAP - used)
        return fail("output exceeds buffer");
    copy(dst + used, p, n);
    used += n;
    return 0;
}
static void wordout(const char *s) { emit(s, length(s)); }
static void number(int n) {
    if (n < 0) {
        wordout("-");
        n = -n;
    }
    if (n >= 10)
        number(n / 10);
    char c = '0' + n % 10;
    emit(&c, 1);
}
static int integer(const char *p, int *out) {
    int neg = *p == '-', v = 0;
    if (neg)
        ++p;
    if (!*p)
        return 0;
    while (*p) {
        if (*p < '0' || *p > '9' || v > 1000000)
            return 0;
        v = v * 10 + *p++ - '0';
    }
    *out = neg ? -v : v;
    return 1;
}
static void source_path(unsigned node, char *p) {
    if (!node) {
        p[0] = '/';
        p[1] = 0;
        return;
    }
    source_path(sources->entries[node].parent, p);
    unsigned n = length(p);
    const char *name = source_at(sources->entries[node].name);
    if (n + length(name) + 2 > 256) {
        fail("path too long");
        return;
    }
    if (n > 1)
        p[n++] = '/';
    copy(p + n, name, length(name) + 1);
}
static int path(const char *name, char out[256]) {
    return user_path(input, name, out, 256) ? 0 : fail("invalid path or path too long");
}
/* Return 1 for a regular file, 2 for a directory, 0 for a missing path. */
static int lookup(const char *p, struct stream *s) {
    int node = source_lookup(0, p);
    if (node >= 0) {
        if (source_isdir(node))
            return 2;
        s->p = node == 2   ? input->kernel
               : node == 3 ? input->tools
                           : source_at(sources->entries[node].offset);
        s->n = node == 2 || node == 3 ? length(s->p) : sources->entries[node].size;
        return 1;
    }
    if (starts(p, "/work/")) {
        for (int i = 0; i < VISIBLE_WORK_FILES; ++i)
            if (same(p + 6, input->files[i].name)) {
                if (input->files[i].installed == WORK_DIRECTORY)
                    return 2;
                s->p = input->files[i].text;
                s->n = length(s->p);
                return 1;
            }
        s->p = user_work_file(input, p);
        if (s->p) {
            s->n = length(s->p);
            return 1;
        }
    }
    return 0;
}
static int readfile(const char *name, struct stream *s) {
    char p[256];
    return path(name, p) || lookup(p, s) != 1 ? fail("regular file required") : 0;
}
static int getinput(int n, char **a, struct stream piped, struct stream *s) {
    if (n > 1)
        return fail("expected at most one input file");
    if (n && !same(*a, "-"))
        return readfile(*a, s);
    *s = piped;
    return 0;
}
#define ENTRY(name, usage) {#name, usage},
static const struct {
    const char *name, *usage;
} catalog[] = {SHELL_COMMANDS(ENTRY)};
static int known(const char *name) {
    for (unsigned i = 0; i < sizeof catalog / sizeof *catalog; ++i)
        if (same(catalog[i].name, name))
            return i;
    return -1;
}
static int lines(struct stream s, unsigned *pos, struct stream *line) {
    if (*pos >= s.n)
        return 0;
    unsigned start = *pos;
    while (*pos < s.n && s.p[*pos] != '\n')
        ++*pos;
    line->p = s.p + start;
    line->n = *pos - start;
    if (*pos < s.n)
        ++*pos;
    return 1;
}
static int compare(struct stream a, struct stream b) {
    unsigned n = a.n < b.n ? a.n : b.n;
    for (unsigned i = 0; i < n; ++i)
        if (a.p[i] != b.p[i])
            return (unsigned char)a.p[i] - (unsigned char)b.p[i];
    return (int)a.n - (int)b.n;
}
static void row(struct stream s) {
    emit(s.p, s.n);
    wordout("\n");
}
static char fold(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int match(struct stream s, const char *pat, int icase) {
    unsigned k = length(pat);
    for (unsigned i = 0; i + k <= s.n; ++i) {
        unsigned j = 0;
        while (j < k && (icase ? fold(s.p[i + j]) == fold(pat[j]) : s.p[i + j] == pat[j]))
            ++j;
        if (j == k)
            return 1;
    }
    return 0;
}
#ifndef DEEPSEEK_SHELL
static struct stream rows[8192], temp[8192];
static void sortrows(int n, int reverse) {
    for (int width = 1; width < n; width *= 2) {
        for (int l = 0; l < n; l += 2 * width) {
            int mid = l + width < n ? l + width : n, end = l + 2 * width < n ? l + 2 * width : n,
                i = l, j = mid;
            for (int k = l; k < end; ++k)
                temp[k] =
                    (j == end || (i < mid && compare(rows[i], rows[j]) * (reverse ? -1 : 1) <= 0))
                        ? rows[i++]
                        : rows[j++];
        }
        for (int i = 0; i < n; ++i)
            rows[i] = temp[i];
    }
}
static int filter(const char *cmd, int n, char **a, struct stream in) {
    int head = same(cmd, "head"), tail = same(cmd, "tail"), grep = same(cmd, "grep"),
        wc = same(cmd, "wc");
    int sort = same(cmd, "sort"), uniq = same(cmd, "uniq"), cut = same(cmd, "cut");
    int count = 10, field = 0;
    char delim = '\t';
    const char *opts = "", *pattern = "";
    if (head || tail) {
        if (n >= 2 && same(a[0], "-n")) {
            if (!integer(a[1], &count) || count < 0)
                return fail("invalid line count");
            a += 2;
            n -= 2;
        }
    } else if (cut) {
        while (n >= 2 && a[0][0] == '-') {
            if (same(a[0], "-d") && length(a[1]) == 1)
                delim = *a[1];
            else if (same(a[0], "-f") && integer(a[1], &field) && field > 0) {
            } else
                return fail("invalid cut option");
            a += 2;
            n -= 2;
        }
        if (!field)
            return fail("cut requires -f N");
    } else if (n && a[0][0] == '-' && a[0][1]) {
        opts = a[0] + 1;
        ++a;
        --n;
        const char *valid = grep ? "invFc" : wc ? "lwc" : sort ? "ru" : uniq ? "c" : "";
        for (const char *p = opts; *p; ++p) {
            const char *q = valid;
            while (*q && *q != *p)
                ++q;
            if (!*q)
                return fail("unsupported option");
        }
    }
    if (grep) {
        if (!n)
            return fail("grep requires a literal pattern");
        pattern = *a++;
        --n;
    }
    struct stream s, line;
    if (getinput(n, a, in, &s))
        return 2;
#define HAS(ch) match((struct stream){opts, length(opts)}, ch, 0)
    if (wc) {
        int l = 0, w = 0, inside = 0, space = 0;
        for (unsigned i = 0; i < s.n; ++i) {
            char c = s.p[i];
            l += c == '\n';
            int blank = c == ' ' || (c >= '\t' && c <= '\r');
            if (!blank && !inside)
                ++w;
            inside = !blank;
        }
        if (!*opts || HAS("l")) {
            number(l);
            space = 1;
        }
        if (!*opts || HAS("w")) {
            if (space)
                wordout(" ");
            number(w);
            space = 1;
        }
        if (!*opts || HAS("c")) {
            if (space)
                wordout(" ");
            number(s.n);
        }
        wordout("\n");
        return 0;
    }
    unsigned pos = 0;
    int i = 0, hits = 0, start = 0;
    struct stream previous = {0};
    int repeats = 0;
    if (tail) {
        while (lines(s, &pos, &line))
            ++start;
        start -= count;
        pos = 0;
    }
    while (lines(s, &pos, &line)) {
        ++i;
        if (head || tail) {
            if (head ? i <= count : i > start)
                emit(line.p, (unsigned)(s.p + pos - line.p));
        } else if (grep) {
            if (match(line, pattern, HAS("i")) != HAS("v")) {
                ++hits;
                if (!HAS("c")) {
                    if (HAS("n")) {
                        number(i);
                        wordout(":");
                    }
                    row(line);
                }
            }
        } else if (sort) {
            if (i > 8192)
                return fail("sort exceeds 8192 lines");
            rows[i - 1] = line;
        } else if (uniq) {
            if (repeats && compare(previous, line)) {
                if (HAS("c")) {
                    number(repeats);
                    wordout(" ");
                }
                row(previous);
                repeats = 0;
            }
            previous = line;
            ++repeats;
        } else if (cut) {
            int f = 1, has = 0;
            unsigned begin = 0;
            for (unsigned j = 0; j <= line.n; ++j)
                if (j == line.n || line.p[j] == delim) {
                    if (j < line.n)
                        has = 1;
                    if (f++ == field)
                        emit(line.p + begin, j - begin);
                    begin = j + 1;
                }
            if (!has && field != 1)
                emit(line.p, line.n);
            wordout("\n");
        } else {
            number(i);
            wordout("\t");
            row(line);
        }
        if (error)
            return 2;
    }
    if (sort) {
        sortrows(i, HAS("r"));
        for (int j = 0; j < i; ++j)
            if (!HAS("u") || !j || compare(rows[j - 1], rows[j]))
                row(rows[j]);
    }
    if (uniq && repeats) {
        if (HAS("c")) {
            number(repeats);
            wordout(" ");
        }
        row(previous);
    }
    if (grep && HAS("c")) {
        number(hits);
        wordout("\n");
    }
    return grep && !hits ? 1 : 0;
}
#endif
static const char *leaf(const char *p) {
    const char *last = p;
    while (*p) {
        if (*p == '/')
            last = p + 1;
        ++p;
    }
    return last;
}
static int glob(const char *p, const char *s) {
    const char *star = 0, *again = 0;
    while (*s) {
        if (*p == '?' || (*p && *p == *s)) {
            ++p;
            ++s;
        } else if (*p == '*') {
            star = p++;
            again = s;
        } else if (star) {
            p = star + 1;
            s = ++again;
        } else
            return 0;
    }
    while (*p == '*')
        ++p;
    return !*p;
}
typedef void (*visitor)(const char *,int,void *);
static void visit_visible(visitor visit,void *ctx) {
    char p[256];
#define VISIT(path, kind) visit(path,kind,ctx)
    for (unsigned i = 0; i < sources->count && !error; ++i) {
        source_path(i, p);
        VISIT(p, source_isdir(i) ? 2 : 1);
    }
    for (int i = 0; i < VISIBLE_WORK_FILES; ++i)
        if (input->files[i].name[0]) {
            copy(p, "/work/", 6);
            copy(p + 6, input->files[i].name, length(input->files[i].name) + 1);
            VISIT(p, input->files[i].installed == WORK_DIRECTORY ? 2 : 1);
        }
    for (unsigned i = 0; i < input->work_count; ++i) {
        copy(p, "/work/", 6);
        unsigned n = length(input->work[i].name);
        copy(p + 6, input->work[i].name, n);
        copy(p + 6 + n, ".c", 3);
        VISIT(p, 1);
        copy(p + 6 + n, ".build", 7);
        VISIT(p, 1);
    }
    if(USER_APPLETS->recipe[0])VISIT("/work/dsh.build",1);
    for(unsigned i=0;i<APPLET_COUNT;++i){
        const char *g=USER_APPLETS->entries[i].group;if(!*g)continue;
        copy(p,"/work/applet-",13);unsigned n=length(g);copy(p+13,g,n);copy(p+13+n,".c",3);VISIT(p,1);
    }
    if (input->counts[0])
        VISIT("/work/counts.txt", 1);
    if (input->answer[0])
        VISIT("/work/answer.txt", 1);
    if (input->has_scratch)
        VISIT("/work/scratch.txt", 1);
#undef VISIT
}
#ifndef DEEPSEEK_SHELL
struct listing {
    const char *search;
    int icase, names_only, hits;
    char base[256];
    const char *pattern;
    int type, recursive;
};
static void show(struct listing *l, const char *p, int kind) {
    if (!starts(p, l->base))
        return;
    unsigned k = length(l->base);
    const char *child = p + k;
    if (*child && k > 1 && *child != '/')
        return;
    if (*child == '/')
        ++child;
    if (l->search) {
        struct stream file, line;
        unsigned pos = 0, lineno = 0;
        if (kind != 1 || readfile(p, &file))
            return;
        while (!error && lines(file, &pos, &line)) {
            ++lineno;
            if (!match(line, l->search, l->icase))
                continue;
            ++l->hits;
            wordout(p);
            if (l->names_only) {
                wordout("\n");
                break;
            }
            wordout(":");
            number(lineno);
            wordout(":");
            row(line);
        }
        return;
    }
    if (!l->recursive) {
        if (!*child || leaf(child) != child)
            return;
        wordout(child);
        if (kind == 2)
            wordout("/");
        wordout("\n");
    } else if ((!l->type || kind == l->type) && glob(l->pattern, leaf(p))) {
        wordout(p);
        wordout("\n");
    }
}
static void show_listing(const char *name,int kind,void *ctx){show(ctx,name,kind);}
static int list(int n, char **a, int recursive) {
    struct listing l = {0};
    if (recursive == 2) {
        if (n && a[0][0] == '-') {
            for (const char *p = a[0] + 1; *p; ++p) {
                if (*p == 'i')
                    l.icase = 1;
                else if (*p == 'l')
                    l.names_only = 1;
                else
                    return fail("unsupported rg option");
            }
            ++a;
            --n;
        }
        if (!n)
            return fail("rg requires literal text");
        l.search = *a++;
        --n;
    }
    l.pattern = "*";
    l.type = 0;
    l.recursive = recursive;
    const char *base = ".";
    if (n && a[0][0] != '-') {
        base = *a++;
        --n;
    }
    while (n >= 2 && recursive) {
        if (same(a[0], "-name"))
            l.pattern = a[1];
        else if (same(a[0], "-type") && (same(a[1], "d") || same(a[1], "f")))
            l.type = *a[1] == 'd' ? 2 : 1;
        else
            return fail("invalid find option");
        n -= 2;
        a += 2;
    }
    if (n || path(base, l.base))
        return fail("invalid list arguments");
    struct stream s;
    int kind = lookup(l.base, &s);
    if (!kind)
        return fail("path not found");
    if (!recursive && kind == 1) {
        wordout(base);
        wordout("\n");
        return 0;
    }
    visit_visible(show_listing,&l);
    return l.search && !l.hits;
}
#endif
static int change(unsigned op, const char *name, const char *origin, struct stream data,
                  int append) {
    char p[256], from[256];
    struct stream old = {0};
    if (output->change)
        return fail("one file change per pipeline");
    if (path(name, p) || !starts(p, "/work/") || length(p) >= 54)
        return fail("expected a writable /work path");
    if (origin && (path(origin, from) || !starts(from, "/work/") || length(from) >= 54))
        return fail("invalid source path");
    if (append && lookup(p, &old) == 2)
        return fail("destination is a directory");
    unsigned prefix = append && old.p ? old.n : 0;
    if (prefix + data.n >= sizeof output->payload)
        return fail("file exceeds 4095 bytes");
    if (prefix)
        copy(output->payload, old.p, prefix);
    if (data.n)
        copy(output->payload + prefix, data.p, data.n);
    output->payload[prefix + data.n] = 0;
    copy(output->target, p, length(p) + 1);
    if (origin)
        copy(output->origin, from, length(from) + 1);
    output->change = op;
    return 0;
}
#ifndef DEEPSEEK_SHELL
static int format(const char *fmt, int n, char **a) {
    int i = 0;
    do {
        int old = i;
        for (const char *p = fmt; *p; ++p) {
            char c = *p;
            if (c == '%' && p[1]) {
                c = *++p;
                if (c == 's') {
                    if (i < n)
                        wordout(a[i++]);
                    continue;
                }
                if (c != '%')
                    return fail("printf supports %s and %%");
            } else if (c == '\\' && p[1]) {
                c = *++p;
                if (c == 'n')
                    c = '\n';
                else if (c == 't')
                    c = '\t';
                else if (c != '\\')
                    return fail("unsupported escape");
            }
            emit(&c, 1);
        }
        if (i == old)
            break;
    } while (i < n && !error);
    return 0;
}
static int translate(int n, char **a, struct stream in) {
    int del = n && same(a[0], "-d");
    if (del) {
        --n;
        ++a;
    }
    if (n != (del ? 1 : 2))
        return fail("tr [-d] SET [SET]");
    unsigned char sets[2][256];
    unsigned widths[2] = {0};
    for (int i = 0; i < n; ++i) {
        const unsigned char *p = (const unsigned char *)a[i];
        while (*p) {
            unsigned lo = *p++, hi = lo;
            if (*p == '-' && p[1]) {
                ++p;
                hi = *p++;
                if (hi < lo)
                    return fail("invalid range");
            }
            for (unsigned j = lo; j <= hi; ++j) {
                if (widths[i] == 256)
                    return fail("set too long");
                sets[i][widths[i]++] = j;
            }
        }
        if (!widths[i])
            return fail("empty set");
    }
    for (unsigned i = 0; i < in.n; ++i) {
        unsigned char c = in.p[i];
        unsigned j = 0;
        while (j < widths[0] && sets[0][j] != c)
            ++j;
        if (j < widths[0]) {
            if (del)
                continue;
            c = sets[1][j < widths[1] ? j : widths[1] - 1];
        }
        emit((const char *)&c, 1);
    }
    return 0;
}
#include "text_filters.h"
static int apply_args(int n, char **args, struct stream stream);
static int stage(int argc, char **argv, struct stream in) {
    const char *cmd = argv[0];
    int n = argc - 1;
    char **a = argv + 1;
    struct stream data = {0}, other = {0};
    char p[256];
    if (same(cmd, "commands") || same(cmd, "which")) {
        if (!n && same(cmd, "commands")) {
            for (unsigned i = 0; i < sizeof catalog / sizeof *catalog; ++i) {
                wordout(catalog[i].name);
                wordout(" ");
                wordout(catalog[i].usage);
                wordout("\n");
            }
            return 0;
        }
        if (n != 1)
            return fail("command name required");
        int i = known(*a);
        if (i < 0)
            return 1;
        wordout(catalog[i].name);
        if (same(cmd, "commands")) {
            wordout(" ");
            wordout(catalog[i].usage);
        }
        wordout("\n");
        return 0;
    }
    if (same(cmd, "true") || same(cmd, "false"))
        return same(cmd, "false");
    if (same(cmd, "echo")) {
        int newline = !(n && same(*a, "-n"));
        if (!newline) {
            ++a;
            --n;
        }
        for (int i = 0; i < n; ++i) {
            if (i)
                wordout(" ");
            wordout(a[i]);
        }
        if (newline)
            wordout("\n");
        return 0;
    }
    if (same(cmd, "printf"))
        return n ? format(*a, n - 1, a + 1) : fail("format required");
    if (same(cmd, "seq")) {
        int first = 1, step = 1, last;
        if (n < 1 || n > 3 || !integer(a[n - 1], &last) || (n > 1 && !integer(a[0], &first)) ||
            (n == 3 && !integer(a[1], &step)) || !step)
            return fail("invalid sequence");
        for (int k = first; !error && (step > 0 ? k <= last : k >= last); k += step) {
            number(k);
            wordout("\n");
        }
        return 0;
    }
    if (same(cmd, "cat")) {
        if (!n)
            return emit(in.p, in.n);
        for (int i = 0; i < n; ++i) {
            if (same(a[i], "-"))
                data = in;
            else if (readfile(a[i], &data))
                return 2;
            emit(data.p, data.n);
        }
        return 0;
    }
    if (same(cmd, "sed"))
        return substitute(n, a, in);
    if (same(cmd, "awk"))
        return project_field(n, a, in);
    if (same(cmd, "diff"))
        return diff_file(n, a);
    if (same(cmd, "rg"))
        return list(n, a, 2);
    if (same(cmd, "sha256sum")) {
        if (getinput(n, a, in, &data))
            return 2;
        br_sha256_context ctx;
        unsigned char hash[32];
        const char hex[] = "0123456789abcdef";
        br_sha256_init(&ctx);
        br_sha256_update(&ctx, data.p, data.n);
        br_sha256_out(&ctx, hash);
        for (unsigned i = 0; i < 32; ++i) {
            emit(hex + (hash[i] >> 4), 1);
            emit(hex + (hash[i] & 15), 1);
        }
        wordout("  ");
        wordout(n ? *a : "-");
        wordout("\n");
        return 0;
    }
    if (same(cmd, "xargs"))
        return apply_args(n, a, in);
    if (same(cmd, "tr"))
        return translate(n, a, in);
    if (same(cmd, "basename") || same(cmd, "dirname")) {
        if (n < 1 || n > (same(cmd, "basename") ? 2 : 1))
            return fail("invalid path arguments");
        char *s = a[0];
        unsigned k = length(s);
        while (k > 1 && s[k - 1] == '/')
            s[--k] = 0;
        if (same(cmd, "basename")) {
            const char *b = same(s, "/") ? s : leaf(s);
            k = length(b);
            if (n == 2 && length(a[1]) < k && same(b + k - length(a[1]), a[1]))
                k -= length(a[1]);
            emit(b, k);
        } else {
            const char *b = leaf(s);
            if (b == s)
                wordout(".");
            else {
                unsigned len = b - s;
                while (len > 1 && s[len - 1] == '/')
                    --len;
                emit(s, len);
            }
        }
        wordout("\n");
        return 0;
    }
    if (same(cmd, "ls") || same(cmd, "find"))
        return list(n, a, same(cmd, "find"));
    if (same(cmd, "pwd")) {
        if (n)
            return fail("pwd takes no arguments");
        wordout(cwd);
        wordout("\n");
        return 0;
    }
    if (same(cmd, "cd")) {
        if (n > 1 || path(n ? *a : "/", p) || lookup(p, &data) != 2)
            return fail("not a directory");
        int node = source_lookup(0, p);
        output->cwd = node >= 0 ? node : source_lookup(0, "/work");
        if (node < 0)
            copy(output->shell_dir, p, length(p) + 1);
        return 0;
    }
    if (same(cmd, "stat")) {
        if (n != 1 || path(*a, p))
            return fail("stat PATH");
        int kind = lookup(p, &data);
        if (!kind)
            return fail("path not found");
        wordout(kind == 2 ? "directory " : "file ");
        if (kind == 1) {
            number(data.n);
            wordout(" bytes ");
        }
        wordout(p);
        wordout("\n");
        return 0;
    }
    if (same(cmd, "test")) {
        if (n == 3 && same(a[1], "="))
            return !same(a[0], a[2]);
        if (n != 2 || path(a[1], p))
            return fail("invalid test");
        int kind = lookup(p, &data);
        if (same(*a, "-e"))
            return !kind;
        if (same(*a, "-f"))
            return kind != 1;
        if (same(*a, "-d"))
            return kind != 2;
        return fail("unknown predicate");
    }
    if (same(cmd, "cmp")) {
        if (n != 2 || readfile(a[0], &data) || readfile(a[1], &other))
            return fail("cmp requires two files");
        return !!compare(data, other);
    }
    if (same(cmd, "cp")) {
        if (n != 2 || readfile(a[0], &data))
            return fail("cp SOURCE DEST");
        return change(SH_WRITE, a[1], 0, data, 0);
    }
    if (same(cmd, "mv"))
        return n == 2 ? change(SH_MOVE, a[1], a[0], data, 0) : fail("mv SOURCE DEST");
    if (same(cmd, "tee")) {
        int append = n == 2 && same(*a, "-a");
        if (n != 1 + append)
            return fail("tee [-a] FILE");
        emit(in.p, in.n);
        return change(SH_WRITE, a[append], 0, in, append);
    }
    if (same(cmd, "touch") || same(cmd, "rm") || same(cmd, "mkdir") || same(cmd, "rmdir")) {
        if (n != 1)
            return fail("one path required");
        if (same(cmd, "touch") && !path(*a, p) && lookup(p, &data) == 1)
            return 0;
        return change(same(cmd, "rm")      ? SH_REMOVE
                      : same(cmd, "mkdir") ? SH_MKDIR
                      : same(cmd, "rmdir") ? SH_RMDIR
                                           : SH_WRITE,
                      *a, 0, (struct stream){0}, 0);
    }
    if (known(cmd) >= 0)
        return filter(cmd, n, a, in);
    return fail("unknown shell command");
}
/* xargs passes data as arguments, never reparses a command or an operator. */
static int apply_args(int n, char **a, struct stream in) {
    static char fields[CAP + 1];
    int per = 64, delimiter = -1;
    while (n >= 2 && a[0][0] == '-') {
        if (same(a[0], "-n")) {
            if (!integer(a[1], &per) || per < 1 || per > 64)
                return fail("xargs -n requires 1..64");
        } else if (same(a[0], "-d")) {
            if (same(a[1], "\\n"))
                delimiter = '\n';
            else if (length(a[1]) == 1)
                delimiter = (unsigned char)*a[1];
            else
                return fail("invalid xargs delimiter");
        } else
            return fail("unsupported xargs option");
        n -= 2;
        a += 2;
    }
    if (!n || n + per > 128 || known(a[0]) < 0 || same(a[0], "xargs") || same(a[0], "cd"))
        return fail("expected a supported xargs command");
    char *argv[128];
    char fixed[2048];
    unsigned pos = 0;
    int rc = 0;
    while (pos < in.n && !error) {
        unsigned used = 0;
        int count = 0;
        while (count < per && pos < in.n) {
            if (delimiter < 0) {
                while (pos < in.n && (unsigned char)in.p[pos] <= 32)
                    ++pos;
                if (pos == in.n)
                    break;
            }
            argv[n + count++] = fields + used;
            char quote = 0;
            while (pos < in.n) {
                char c = in.p[pos++];
                if (delimiter >= 0) {
                    if ((unsigned char)c == delimiter)
                        break;
                } else {
                    if (!quote && (unsigned char)c <= 32)
                        break;
                    if (c == quote && quote) {
                        quote = 0;
                        continue;
                    }
                    if (!quote && (c == '\x27' || c == '"')) {
                        quote = c;
                        continue;
                    }
                    if (c == '\\' && quote != '\x27') {
                        if (pos == in.n)
                            return fail("incomplete xargs escape");
                        c = in.p[pos++];
                    }
                }
                fields[used++] = c;
            }
            if (quote)
                return fail("unclosed xargs quote");
            fields[used++] = 0;
        }
        if (count) {
            unsigned offset = 0;
            for (int i = 0; i < n; ++i) {
                unsigned bytes = length(a[i]) + 1;
                if (bytes > sizeof fixed - offset)
                    return fail("xargs command too long");
                argv[i] = fixed + offset;
                copy(argv[i], a[i], bytes);
                offset += bytes;
            }
            int ret = stage(n + count, argv, (struct stream){"", 0});
            if (ret)
                rc = ret;
            if (ret > 1)
                return ret;
        }
    }
    return rc;
}

#else
#include "ds_runtime.h"
#endif
/* Operators are tokens even without intervening blanks; quoted operators are data. */
struct token {
    char *s;
    int op;
};
static char decoded[2049];
static struct token tokens[128];
static int lex(const char *p) {
    char *d = decoded;
    int n = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!*p)
            break;
        if (n == 128)
            return -1;
        struct token *t = &tokens[n++];
        t->s = d;
        t->op = 0;
        if (*p == '|' || *p == '<' || *p == '>') {
            t->op = *p++;
            if (t->op == '>' && *p == '>') {
                ++p;
                t->op = 'a';
            }
            continue;
        }
        char quote = 0;
        while (*p) {
            char c = *p++;
            if (!quote && (c == ' ' || c == '\t' || c == '|' || c == '<' || c == '>')) {
                --p;
                break;
            }
            if (c == quote) {
                quote = 0;
                continue;
            }
            if (!quote && (c == '\'' || c == '"')) {
                quote = c;
                continue;
            }
            if (c == '\\' && quote != '\'' && *p && (!quote || *p == quote || *p == '\\'))
                c = *p++;
            *d++ = c;
        }
        if (quote)
            return -1;
        *d++ = 0;
    }
    return n;
}
void _start(struct tool_input *in, struct tool_output *out) {
    input = in;
    output = out;
    out->cwd = in->cwd;
    if (in->shell_dir[0]) {
        copy(cwd, in->shell_dir, length(in->shell_dir) + 1);
        copy(out->shell_dir, cwd, length(cwd) + 1);
    } else
        source_path(in->cwd, cwd);
    int nt = lex(in->command), pos = 0, status = 0, bank = 0;
    struct stream stream = {in->pipe_data, in->pipe_bytes};
    if (nt <= 0)
        status = fail("invalid shell syntax");
    while (pos < nt && !error) {
        char *args[128], *target = 0;
        int argc = 0, append = 0, redirect_in = 0;
        while (pos < nt && tokens[pos].op != '|') {
            struct token t = tokens[pos++];
            if (!t.op) {
                args[argc++] = t.s;
                continue;
            }
            if (pos == nt || tokens[pos].op) {
                fail("redirection needs a path");
                break;
            }
            char *name = tokens[pos++].s;
            if (t.op == '<') {
                if (redirect_in++ || bank || readfile(name, &stream)) {
                    fail("invalid input redirection");
                    break;
                }
            } else {
                if (target) {
                    fail("duplicate output redirection");
                    break;
                }
                target = name;
                append = t.op == 'a';
            }
        }
        if (error)
            break;
        int pipe = pos < nt;
        if (!argc || (pipe && (target || pos + 1 == nt)) ||
            (same(args[0], "cd") && (pipe || bank))) {
            status = fail("invalid pipeline");
            break;
        }
        dst = buffers[bank % 2];
        used = 0;
        if (same(args[0], "cd"))
            out->shell_dir[0] = 0;
        status = stage(argc, args, stream);
        stream.p = dst;
        stream.n = used;
        if (error || status > 1)
            break;
        if (target) {
            if (!status)
                status = change(SH_WRITE, target, 0, stream, append);
            stream.n = 0;
        }
        if (pipe)
            ++pos;
        ++bank;
    }
    if (error) {
        out->length = 0;
        dst = out->text;
        used = 0;
        wordout("sh: ");
        wordout(error);
        wordout("\n");
        out->length = used;
        status = 2;
    } else {
        copy(out->text, stream.p, stream.n);
        out->length = stream.n;
    }
    if (status)
        out->change = 0;
    out->status = status;
    __asm__ volatile(".long 0xd4000001");
}
