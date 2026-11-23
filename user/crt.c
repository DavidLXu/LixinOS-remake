#include "api.h"
#include "app.h"
static struct tool_output *terminal;
int putchar(int c) {
    if (terminal->length>=sizeof terminal->text) { terminal->truncated=1; return -1; }
    terminal->text[terminal->length++]=(char)c; return (unsigned char)c;
}
int puts(const char *s) { while (*s) if (putchar(*s++)<0) return -1; return putchar('\n'); }
void _start(struct tool_input *in,struct tool_output *out) {
    char *args[65], *p=in->command; int n=1;
    terminal=out;
    args[0]=in->program;
    while (*p) {
        while (*p==' ') ++p;
        if (!*p) break;
        if (n==64) { puts("too many arguments"); goto done; }
        args[n++]=p; while (*p && *p!=' ') ++p;
        if (*p) *p++=0;
    }
    args[n]=0; main(n,args);
done:
    __asm__ volatile(".long 0xd4000001");
}
