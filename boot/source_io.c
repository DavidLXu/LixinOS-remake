#include "sourcefs.h"
struct reader_fd { const char *data; unsigned pos, size; };
static struct reader_fd readers[48];
static const char *active_tools;
static int source_open(const char *name) {
    int node=source_lookup(1,name);
    if (node<0 || source_isdir(node)) { errno=ENOENT; return -1; }
    for (unsigned i=0;i<sizeof readers/sizeof readers[0];++i) {
        if (readers[i].data) continue;
        const struct source_entry *e=sources->entries+node;
        const char *data=source_at(e->offset);
        unsigned size=e->size;
        if (node==2 && machine.source_len) { data=machine.source; size=machine.source_len; }
        if (node==3 && active_tools) { data=active_tools; size=strlen(data); }
        readers[i]=(struct reader_fd){data,0,size};
        return i+4;
    }
    errno=EMFILE; return -1;
}
static struct reader_fd *reader(int fd) {
    if (fd<4 || (unsigned)(fd-4)>=sizeof readers/sizeof readers[0] || !readers[fd-4].data) return NULL;
    return readers+fd-4;
}
int _read(int fd,char *out,int n) {
    struct reader_fd *r=reader(fd);
    if (!r || n<0) { errno=EBADF; return -1; }
    if ((unsigned)n>r->size-r->pos) n=r->size-r->pos;
    memcpy(out,r->data+r->pos,n); r->pos+=n; return n;
}
static int source_valid(void) {
    if (sources->magic!=0x53524331 || sources->version!=1 || sources->bytes<16 || sources->bytes>SOURCE_MAX ||
        sources->count<4 || sources->count>(sources->bytes-16)/sizeof(struct source_entry)) return 0;
    for (unsigned i=0;i<sources->count;++i) {
        const struct source_entry *e=&sources->entries[i];
        if ((i && e->parent>=i) || e->name>=sources->bytes ||
            !memchr(source_at(e->name),0,sources->bytes-e->name)) return 0;
        if (e->offset && (e->offset>=sources->bytes || e->size>=sources->bytes-e->offset ||
                          source_at(e->offset)[e->size])) return 0;
    }
    return 1;
}
