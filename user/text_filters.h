/* The accepted sed and awk grammars are deliberately small and fully checked. */
static int substitute(int n, char **args, struct stream in) {
    int inplace = n && same(args[0], "-i");
    if (inplace) {
        --n;
        ++args;
    }
    if (n < 1 || n > 2 || (inplace && n != 2))
        return fail("sed [-i] s/OLD/NEW/[g] [FILE]");
    char *expr = *args++;
    --n;
    if (expr[0] != 's' || !expr[1])
        return fail("expected sed substitution");
    char delim = expr[1], *old = expr + 2, *replacement = old;
    while (*replacement && *replacement != delim)
        ++replacement;
    if (!*replacement || replacement == old)
        return fail("invalid sed search");
    *replacement++ = 0;
    char *end = replacement;
    while (*end && *end != delim)
        ++end;
    if (!*end)
        return fail("missing sed delimiter");
    *end++ = 0;
    int global = same(end, "g");
    if (*end && !global)
        return fail("invalid sed flag");
    struct stream s;
    if (getinput(n, args, in, &s))
        return 2;
    unsigned k = length(old);
    int changed = 0;
    for (unsigned i = 0; i < s.n && !error;) {
        if (!changed && i + k <= s.n &&
            compare((struct stream){s.p + i, k}, (struct stream){old, k}) == 0) {
            wordout(replacement);
            i += k;
            changed = !global;
        } else {
            char c = s.p[i++];
            emit(&c, 1);
            if (c == '\n')
                changed = 0;
        }
    }
    if (inplace && !error) {
        int rc = change(SH_WRITE, *args, 0, (struct stream){dst, used}, 0);
        used = 0;
        return rc;
    }
    return error ? 2 : 0;
}
static int project_field(int n, char **args, struct stream in) {
    if (n < 1 || n > 2)
        return fail("awk '{print $N}' [FILE]");
    char *expr = *args++;
    --n;
    if (!starts(expr, "{print ") || length(expr) < 9 || expr[length(expr) - 1] != '}')
        return fail("unsupported awk expression");
    expr[length(expr) - 1] = 0;
    expr += 7;
    int nr = same(expr, "NR"), nf = same(expr, "NF"), field = 0;
    if (!nr && !nf && (*expr != '$' || !integer(expr + 1, &field) || field < 0))
        return fail("expected $N, NF, or NR");
    struct stream s, line;
    if (getinput(n, args, in, &s))
        return 2;
    unsigned pos = 0;
    int lineno = 0;
    while (lines(s, &pos, &line)) {
        ++lineno;
        if (nr)
            number(lineno);
        else if (!nf && !field)
            emit(line.p, line.n);
        else {
            unsigned i = 0;
            int fields = 0;
            while (i < line.n) {
                while (i < line.n && (line.p[i] == ' ' || line.p[i] == '\t'))
                    ++i;
                if (i == line.n)
                    break;
                unsigned start = i;
                while (i < line.n && line.p[i] != ' ' && line.p[i] != '\t')
                    ++i;
                if (++fields == field && !nf)
                    emit(line.p + start, i - start);
            }
            if (nf)
                number(fields);
        }
        wordout("\n");
        if (error)
            return 2;
    }
    return 0;
}
static int diff_file(int n, char **a) {
    if (n == 3 && same(*a, "-u")) {
        ++a;
        --n;
    }
    struct stream x, y;
    if (n != 2 || readfile(a[0], &x) || readfile(a[1], &y))
        return fail("diff [-u] OLD NEW");
    if (!compare(x, y))
        return 0;
    /* Emit a valid single unified hunk; unchanged prefix and suffix stay outside it. */
    struct stream data[2] = {x, y};
    struct stream *table[2] = {rows, temp};
    int counts[2] = {0};
    for (int j = 0; j < 2; ++j) {
        unsigned pos = 0;
        struct stream l;
        while (lines(data[j], &pos, &l)) {
            if (counts[j] == 8192)
                return fail("diff exceeds 8192 lines");
            l.n = data[j].p + pos - l.p;
            table[j][counts[j]++] = l;
        }
    }
    int start = 0, suffix = 0;
    while (start < counts[0] && start < counts[1] && !compare(rows[start], temp[start]))
        ++start;
    while (suffix + start < counts[0] && suffix + start < counts[1] &&
           !compare(rows[counts[0] - 1 - suffix], temp[counts[1] - 1 - suffix]))
        ++suffix;
    wordout("--- ");
    wordout(a[0]);
    wordout("\n+++ ");
    wordout(a[1]);
    wordout("\n@@ -");
    for (int j = 0; j < 2; ++j) {
        int count = counts[j] - start - suffix;
        number(start + (count > 0));
        wordout(",");
        number(count);
        wordout(j ? " @@\n" : " +");
    }
    for (int j = 0; j < 2; ++j)
        for (int i = start; i < counts[j] - suffix; ++i) {
            struct stream l = table[j][i];
            wordout(j ? "+" : "-");
            emit(l.p, l.n);
            if (l.p[l.n - 1] != '\n')
                wordout("\n\\ No newline at end of file\n");
        }
    return 1;
}
