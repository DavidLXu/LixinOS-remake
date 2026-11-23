/* Trusted guest verifier. Enumerate the source directory independently of the
   producer and reducer, and count newlines in the source bytes themselves. */
#include "app.h"
#include "sourcefs.h"

static char rows[8192], answer[320];
static int read_text(const char *path,char *out,unsigned cap) {
    int fd=file_open(path);
    if (fd<0) return 0;
    unsigned used=0;
    int n=0;
    while (used<cap-1 && (n=file_read(fd,out+used,cap-1-used))>0) used+=n;
    char extra;
    int complete=n>=0 && file_read(fd,&extra,1)==0;
    file_close(fd); out[used]=0;
    for (unsigned i=0;i<used;++i) if (!out[i]) return 0;
    return complete;
}
static unsigned len(const char *p) { unsigned n=0; while (p[n]) ++n; return n; }
static int compare(const char *a,const char *b) {
    while (*a && *a==*b) { ++a; ++b; }
    return (unsigned char)*a-(unsigned char)*b;
}
static int fail(const char *why,const char *name) {
    while (*why) putchar(*why++);
    puts(name); return 1;
}
/* Parse a terminated path/count pair. Clear separators in this private copy. */
static int row(char **cursor,char **name,unsigned *number) {
    char *p=*cursor; *name=p;
    while (*p && *p!=' ' && *p!='\n') ++p;
    if (p==*name || *p!=' ') return 0;
    *p++=0;
    if (*p<'0' || *p>'9') return 0;
    unsigned value=0;
    while (*p>='0' && *p<='9') {
        unsigned digit=*p++-'0';
        if (value>(0xffffffffU-digit)/10) return 0;
        value=value*10+digit;
    }
    if (*p++!='\n') return 0;
    *cursor=p; *number=value; return 1;
}
int main(int argc,char **argv) {
    if (argc!=3 || !read_text(argv[1],rows,sizeof rows) || !read_text(argv[2],answer,sizeof answer))
        return fail("unreadable result files","");
    /* Each eligible row needs at least "/src/user/.c 0\n". */
    unsigned seen[sizeof rows/14+1],used=0;
    int dir=source_lookup(0,"/src/user");
    if (dir<0) return fail("unsupported source directory","");
    char *p=rows,*best=0; unsigned largest=0;
    while (*p) {
        char *path; unsigned count;
        if (!row(&p,&path,&count)) return fail("malformed count record","");
        const char *prefix="/src/user/",*base=path;
        while (*prefix && *base==*prefix) { ++base; ++prefix; }
        unsigned n=len(base);
        if (*prefix || n<2 || compare(base+n-2,".c")) return fail("unexpected path: ",path);
        int id=source_child(dir,base,n);
        if (id<0 || source_isdir(id)) return fail("unexpected path: ",path);
        for (unsigned j=0;j<used;++j) if (seen[j]==(unsigned)id) return fail("duplicate input: ",path);
        if (used>=sizeof seen/sizeof seen[0]) return fail("too many rows","");
        seen[used++]=id;
        const struct source_entry *e=sources->entries+id;
        const char *data=source_at(e->offset); unsigned actual=0;
        for (unsigned i=0;i<e->size;++i) if (data[i]=='\n') ++actual;
        if (actual!=count) return fail("incorrect count: ",path);
        if (!best || count>largest || (count==largest && compare(path,best)<0)) { best=path; largest=count; }
    }
    for (unsigned i=1;i<sources->count;++i) {
        if (sources->entries[i].parent!=(unsigned)dir || source_isdir(i)) continue;
        const char *name=source_at(sources->entries[i].name); unsigned n=len(name);
        if (n<2 || compare(name+n-2,".c")) continue;
        unsigned j=0; while (j<used && seen[j]!=i) ++j;
        if (j==used) return fail("missing input: /src/user/",name);
    }
    p=answer; char *winner; unsigned count;
    if (!best || !row(&p,&winner,&count) || *p || compare(winner,best) || count!=largest)
        return fail("incorrect maximum","");
    puts("complete counts and maximum verified"); return 0;
}
