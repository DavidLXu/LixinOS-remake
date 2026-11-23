#include "api.h"
#include "app.h"
#define LIXINOS_WORK_INPUT
#include "sourcefs.h"
/* TinyCC emits these calls for initialized arrays and aggregate copies. */
void *memset(void *dst,int value,unsigned long n) {
    unsigned char *p=dst; while (n--) *p++=(unsigned char)value; return dst;
}
void *memcpy(void *dst,const void *src,unsigned long n) {
    unsigned char *p=dst; const unsigned char *q=src; while (n--) *p++=*q++; return dst;
}
static struct tool_output *terminal;
static struct tool_input *input;
static struct {
    const char *bytes;
    unsigned pos, size;
} files[8];
int file_open(const char *path) {
    int node=source_lookup(input->cwd,path);
    const char *workspace=node<0?user_work_file(input,path):0;
    if (!workspace && (node<0 || source_isdir(node))) return -1;
    for (int fd=0;fd<8;++fd) {
        if (files[fd].bytes) continue;
        const struct source_entry *e=workspace?0:&sources->entries[node];
        files[fd].bytes=workspace?workspace:node==2?input->kernel:node==3?input->tools:source_at(e->offset);
        files[fd].size=e?e->size:0;
        if (workspace || node==2 || node==3) {
            unsigned n=0; while (files[fd].bytes[n]) ++n;
            files[fd].size=n;
        }
        files[fd].pos=0;
        return fd;
    }
    return -1;
}
int file_read(int fd,void *buffer,unsigned bytes) {
    if (fd<0 || fd>=8 || !files[fd].bytes) return -1;
    unsigned left=files[fd].size-files[fd].pos;
    if (bytes>left) bytes=left;
    char *out=buffer;
    for (unsigned i=0;i<bytes;++i) out[i]=files[fd].bytes[files[fd].pos++];
    return bytes;
}
int file_close(int fd) {
    if (fd<0 || fd>=8 || !files[fd].bytes) return -1;
    files[fd].bytes=0; return 0;
}
int putchar(int c) {
    if (terminal->length>=sizeof terminal->text) { terminal->truncated=1; return -1; }
    terminal->text[terminal->length++]=(char)c; return (unsigned char)c;
}
int putstr(const char *s) { while (*s) if (putchar(*s++)<0) return -1; return 0; }
int puts(const char *s) { return putstr(s)<0 ? -1 : putchar('\n'); }
void _start(struct tool_input *in,struct tool_output *out) {
    char *args[65], *p=in->command; int n=1;
    terminal=out; input=in; out->status=1;
    args[0]=in->program;
    while (*p) {
        while (*p==' ') ++p;
        if (!*p) break;
        if (n==64) { puts("too many arguments"); goto done; }
        args[n++]=p; while (*p && *p!=' ') ++p;
        if (*p) *p++=0;
    }
    args[n]=0; out->status=main(n,args);
done:
    __asm__ volatile(".long 0xd4000001");
}
