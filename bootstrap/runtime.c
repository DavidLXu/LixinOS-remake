/* The standalone resident's freestanding C runtime. The guest compiler uses
   the same source volume and RAM output descriptors as the seed compiler. */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"

extern void *_sbrk(long);
extern int _open(const char *,int,int),_read(int,char *,int),_write(int,const char *,int);
extern int _close(int),_lseek(int,int,int);
static int last_errno;
int *__errno_location(void) { return &last_errno; }
int open(const char *name,int flags,...) { return _open(name,flags,0); }
int close(int fd) { return _close(fd); }
ssize_t read(int fd,void *p,size_t n) { return n>INT_MAX?-1:_read(fd,p,n); }
off_t lseek(int fd,off_t pos,int where) {
    return pos>INT_MAX || pos<INT_MIN?-1:_lseek(fd,(int)pos,where);
}
int unlink(const char *path) { (void)path; errno=EROFS; return -1; }
char *strerror(int e) { return e==ENOENT?"file not found":"I/O error"; }

struct block { size_t size; struct block *next; };
static struct block *available;
void free(void *p) {
    if (!p) return;
    struct block *b=(struct block *)p-1, *prev=NULL, **link=&available;
    while (*link && (uintptr_t)*link<(uintptr_t)b) { prev=*link; link=&prev->next; }
    b->next=*link; *link=b;
    if (b->next && (char *)b+sizeof *b+b->size==(char *)b->next) {
        b->size+=sizeof *b+b->next->size; b->next=b->next->next;
    }
    if (prev && (char *)prev+sizeof *prev+prev->size==(char *)b) {
        prev->size+=sizeof *b+b->size; prev->next=b->next;
    }
}
void *malloc(size_t n) {
    if (n>SIZE_MAX-65536) return NULL;
    n=(n+15)&~(size_t)15; if (!n) n=16;
    for (;;) {
        for (struct block **link=&available;*link;link=&(*link)->next) {
            struct block *b=*link;
            if (b->size<n) continue;
            if (b->size>=n+sizeof *b+16) {
                struct block *tail=(void *)((char *)(b+1)+n);
                tail->size=b->size-n-sizeof *tail; tail->next=b->next;
                *link=tail; b->size=n;
            } else *link=b->next;
            return b+1;
        }
        size_t grow=n+sizeof(struct block); if (grow<65536) grow=65536;
        struct block *more=_sbrk(grow);
        if (more==(void *)-1) return NULL;
        more->size=grow-sizeof *more; free(more+1);
    }
}
void *calloc(size_t n,size_t size) {
    if (size && n>SIZE_MAX/size) return NULL;
    void *p=malloc(n*size); if (p) memset(p,0,n*size); return p;
}
void *realloc(void *p,size_t n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    size_t old=((struct block *)p)[-1].size;
    if (n<=old) return p;
    void *fresh=malloc(n);
    if (fresh) { memcpy(fresh,p,old); free(p); }
    return fresh;
}

static FILE consoles[3]={{0},{1},{2}};
FILE *stdin=&consoles[0], *stdout=&consoles[1], *stderr=&consoles[2];
FILE *fdopen(int fd,const char *mode) {
    (void)mode; FILE *f=calloc(1,sizeof *f); if (f) f->fd=fd; return f;
}
int fclose(FILE *f) { int r=close(f->fd); free(f); return r; }
int fflush(FILE *f) { (void)f; return 0; }
size_t fwrite(const void *p,size_t size,size_t count,FILE *f) {
    if (!size || count>INT_MAX/size) return 0;
    int r=_write(f->fd,p,size*count); return r<0?0:(size_t)r/size;
}
int fputc(int c,FILE *f) { unsigned char b=c; return fwrite(&b,1,1,f)?b:EOF; }
int fputs(const char *s,FILE *f) { size_t n=strlen(s); return fwrite(s,1,n,f)==n?0:EOF; }
int putchar(int c) { return fputc(c,stdout); }
int puts(const char *s) { return fputs(s,stdout)==EOF?EOF:putchar('\n'); }
int vsnprintf(char *p,size_t n,const char *fmt,va_list a) {
    return stbsp_vsnprintf(p,n>INT_MAX?INT_MAX:(int)n,fmt,a);
}
int snprintf(char *p,size_t n,const char *fmt,...) {
    va_list a; va_start(a,fmt); int r=vsnprintf(p,n,fmt,a); va_end(a); return r;
}
int sprintf(char *p,const char *fmt,...) {
    va_list a; va_start(a,fmt); int r=stbsp_vsprintf(p,fmt,a); va_end(a); return r;
}
static char *print_chunk(const char *bytes,void *file,int n) {
    return fwrite(bytes,1,n,file)==(unsigned)n?(char *)bytes:NULL;
}
int vfprintf(FILE *f,const char *fmt,va_list a) {
    char chunk[STB_SPRINTF_MIN]; return stbsp_vsprintfcb(print_chunk,f,chunk,fmt,a);
}
int fprintf(FILE *f,const char *fmt,...) {
    va_list a; va_start(a,fmt); int r=vfprintf(f,fmt,a); va_end(a); return r;
}
int printf(const char *fmt,...) {
    va_list a; va_start(a,fmt); int r=vfprintf(stdout,fmt,a); va_end(a); return r;
}
/* The two scanf conversions used by cJSON and the HTTP status parser. */
int sscanf(const char *s,const char *format,...) {
    va_list a; va_start(a,format); char *end; int ok=0;
    if (!strcmp(format,"%lg")) {
        double d=strtod(s,&end);
        if (s!=end) { *va_arg(a,double *)=d; ok=1; }
    } else if (!strcmp(format,"HTTP/%*s %d") && !strncmp(s,"HTTP/",5)) {
        const char *start=strchr(s,' ');
        if (start) { long n=strtol(start,&end,10); if (start!=end) { *va_arg(a,int *)=n; ok=1; } }
    }
    va_end(a); return ok;
}
double fabs(double x) { return x<0?-x:x; }
time_t time(time_t *p) { time_t t=*(volatile uint32_t *)0x09010000; if (p) *p=t; return t; }
extern int __secs_to_tm(long long,struct tm *);
struct tm *localtime(const time_t *t) {
    static struct tm value; return __secs_to_tm(*t,&value)?NULL:&value;
}
void abort(void) { puts("resident aborted"); for (;;) {} }
void exit(int code) { (void)code; abort(); }
void __assert_fail(const char *expr,const char *file,int line,const char *fn) {
    fprintf(stderr,"%s:%d %s: %s\n",file,line,fn,expr); abort();
}
/* In-place heap sort avoids allocating during linker section ordering. */
static void exchange(char *a,char *b,size_t size) {
    while (size--) { char c=*a; *a++=*b; *b++=c; }
}
static void sift(char *p,size_t root,size_t n,size_t size,int (*cmp)(const void *,const void *)) {
    while (root<n/2) {
        size_t child=2*root+1;
        if (child+1<n && cmp(p+child*size,p+(child+1)*size)<0) ++child;
        if (cmp(p+root*size,p+child*size)>=0) break;
        exchange(p+root*size,p+child*size,size); root=child;
    }
}
void qsort(void *p,size_t n,size_t size,int (*cmp)(const void *,const void *)) {
    if (!size || n<2) return;
    for (size_t i=n/2;i;) sift(p,--i,n,size,cmp);
    while (--n) { exchange(p,(char *)p+n*size,size); sift(p,0,n,size,cmp); }
}
