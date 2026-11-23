#include "api.h"

static struct tool_output *out;
static void emit(char c) {
    if (out->length < sizeof out->text) out->text[out->length++] = c;
    else out->truncated=1;
}
static void print(const char *text) { while (*text) emit(*text++); }
static void decimal(unsigned n) {
    if (n >= 10) decimal(n/10);
    emit('0'+n%10);
}
static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static char *word(char **tail) {
    char *p = *tail;
    while (*p == ' ') ++p;
    char *start = p;
    while (*p && *p != ' ') ++p;
    if (*p) *p++ = 0;
    *tail = p;
    return start;
}
/* Nodes 0 and 1 are / and /src; 2 and 3 are regular source files. */
static int resolve(int dir, const char *path) {
    if (*path == '/') dir = 0;
    while (*path) {
        while (*path == '/') ++path;
        if (!*path) break;
        const char *p = path;
        while (*path && *path != '/') ++path;
        unsigned n = path-p;
        if (dir > 1) return -1;
        if (n==1 && *p=='.') continue;
        if (n==2 && p[0]=='.' && p[1]=='.') dir=0;
        else if (dir==0 && n==3 && p[0]=='s' && p[1]=='r' && p[2]=='c') dir=1;
        else if (dir==1) {
            const char *name = n==8 ? "kernel.c" : n==7 ? "tools.c" : "";
            unsigned i=0; while (name[i] && name[i]==p[i]) ++i;
            if (i!=n) return -1;
            dir=n==8 ? 2 : 3;
        } else return -1;
        if (*path=='/' && dir>1) return -1;
    }
    return dir;
}
static unsigned positive(const char *p) {
    unsigned n=0;
    while (*p) {
        if (*p<'0' || *p>'9' || n>1000000) return 0;
        n=n*10+(*p++-'0');
    }
    return n;
}
static int contains(const char *line, const char *end, const char *pattern) {
    for (; line < end; ++line) {
        const char *p=pattern, *s=line;
        while (s<end && *p && *p==*s) { ++p; ++s; }
        if (!*p) return 1;
    }
    return 0;
}
static void command(struct tool_input *in) {
    char *tail=in->command, *cmd=word(&tail), *a=word(&tail), *b=word(&tail), *c=word(&tail);
    if (*word(&tail)) { print("too many arguments\n"); return; }
    out->cwd=in->cwd;
    int grep=equal(cmd,"grep"), read=equal(cmd,"read");
    int node=resolve(in->cwd,grep?b:a);
    if (equal(cmd,"pwd") && !*a) { print(in->cwd?"/src\n":"/\n"); return; }
    if (equal(cmd,"cd") && !*b) {
        if (!*a) node=0;
        if (node==0 || node==1) out->cwd=node;
        else print("not a directory\n");
        return;
    }
    if (equal(cmd,"ls") && !*b) {
        if (node==0) print("src/\n");
        else if (node==1) print("kernel.c\ntools.c\n");
        else if (node==2 || node==3) print(node==2?"kernel.c\n":"tools.c\n");
        else print("path not found\n");
        return;
    }
    if ((!read && !grep && !equal(cmd,"cat")) || (!read && *c) || (equal(cmd,"cat") && *b)) {
        print("invalid arguments\n"); return;
    }
    if (node!=2 && node!=3) { print("source file required\n"); return; }
    const char *text=node==2?in->kernel:in->tools;
    if (equal(cmd,"cat")) { print(text); return; }
    unsigned first=1, count=0;
    if (read) {
        first=positive(b); count=positive(c);
        if (!first || !count) { print("read FILE FIRST COUNT\n"); return; }
    } else if (!*a || !*b) { print("grep TEXT FILE\n"); return; }
    for (unsigned line=1; *text; ++line) {
        const char *start=text;
        while (*text && *text!='\n') ++text;
        if (grep ? contains(start,text,a) : line>=first && line-first<count) {
            if (grep) { decimal(line); emit(':'); }
            while (start<text) emit(*start++);
            emit('\n');
        }
        if (*text) ++text;
    }
}
void _start(struct tool_input *in, struct tool_output *output) {
    out=output;
    command(in);
    /* A64 SVC #0 completes the foreground tool. */
    __asm__ volatile(".long 0xd4000001");
}
