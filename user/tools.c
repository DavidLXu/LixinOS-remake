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
#define LIXINOS_WORK_INPUT
#include "sourcefs.h"
static void pathname(unsigned node) {
    if (!node) return;
    pathname(sources->entries[node].parent);
    emit('/'); print(source_at(sources->entries[node].name));
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
    int node=source_lookup(in->cwd,grep?b:a);
    const char *work=node<0?user_work_file(in,grep?b:a):0;
    if (equal(cmd,"pwd") && !*a) { if (in->cwd) pathname(in->cwd); else emit('/'); emit('\n'); return; }
    if (equal(cmd,"cd") && !*b) {
        if (!*a) node=0;
        if (node>=0 && source_isdir(node)) out->cwd=node;
        else print("not a directory\n");
        return;
    }
    if (equal(cmd,"ls") && !*b) {
        if (work) { print(a); emit('\n'); }
        else if (node<0) print("path not found\n");
        else if (node==0) { print("src/\n"); print("work/\n"); }
        else if (node==source_lookup(0,"/work")) {
            for (unsigned i=0;i<in->work_count;++i) {
                print(in->work[i].name); print(".c\n");
                print(in->work[i].name); print(".build\n");
            }
        }
        else if (!source_isdir(node)) { print(source_at(sources->entries[node].name)); emit('\n'); }
        else for (unsigned i=1;i<sources->count;++i) {
            if (sources->entries[i].parent!=(unsigned)node) continue;
            print(source_at(sources->entries[i].name));
            if (source_isdir(i)) emit('/');
            emit('\n');
        }
        return;
    }
    if ((!read && !grep && !equal(cmd,"cat")) || (!read && *c) || (equal(cmd,"cat") && *b)) {
        print("invalid arguments\n"); return;
    }
    if (!work && (node<0 || source_isdir(node))) { print("source file required\n"); return; }
    const char *text=work?work:node==2?in->kernel:node==3?in->tools:source_at(sources->entries[node].offset);
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
