/* Read-only source volume, also mapped read-only at EL0. Indices 0..3 stay
   stable across releases for shell cwd and the two editable translation units. */
#ifndef SOURCEFS_H
#define SOURCEFS_H
#define SOURCE_BASE 0x4e000000UL
#define SOURCE_MAX (32U*1024*1024)
struct source_entry { unsigned parent, name, offset, size; };
struct source_volume {
    unsigned magic, bytes, count, version;
    struct source_entry entries[];
};
#define sources ((const struct source_volume *)SOURCE_BASE)
static const char *source_at(unsigned off) { return (const char *)sources+off; }
static int source_isdir(unsigned n) { return !sources->entries[n].offset; }
static int source_child(unsigned parent,const char *name,unsigned size) {
    for (unsigned i=1;i<sources->count;++i) {
        if (sources->entries[i].parent!=parent) continue;
        const char *p=source_at(sources->entries[i].name);
        unsigned j=0; while (j<size && p[j] && p[j]==name[j]) ++j;
        if (j==size && !p[j]) return i;
    }
    return -1;
}
static int source_lookup(int cwd,const char *path) {
    if (*path=='/') cwd=0;
    if (cwd<0 || (unsigned)cwd>=sources->count) return -1;
    while (*path) {
        if (!source_isdir(cwd)) return -1;
        while (*path=='/') ++path;
        if (!*path) break;
        const char *s=path; while (*path && *path!='/') ++path;
        unsigned n=path-s;
        if (n==1 && *s=='.') continue;
        if (n==2 && s[0]=='.' && s[1]=='.') cwd=sources->entries[cwd].parent;
        else if ((cwd=source_child(cwd,s,n))<0) return -1;
    }
    return cwd;
}
#ifdef LIXINOS_WORK_INPUT
/* Resolve the parent using the same cwd and directory rules as /src. */
static const char *user_work_file(const struct tool_input *in,const char *path) {
    const char *leaf=path;
    for (const char *p=path;*p;++p) if (*p=='/') leaf=p+1;
    int parent=in->cwd;
    if (leaf!=path) {
        char dir[256]; unsigned len=leaf-path;
        if (len>=sizeof dir) return 0;
        for (unsigned i=0;i<len;++i) dir[i]=path[i];
        dir[len]=0; parent=source_lookup(in->cwd,dir);
    }
    int work=source_lookup(0,"/work");
    if (parent<0 || parent!=work) return 0;
    for (unsigned i=0;i<in->work_count;++i) {
        const char *name=in->work[i].name,*p=leaf;
        while (*name && *name==*p) { ++name; ++p; }
        if (*name || *p++!='.') continue;
        if (p[0]=='c' && !p[1]) return in->work[i].code;
        const char *ext="build";
        while (*ext && *ext==*p) { ++ext; ++p; }
        if (!*ext && !*p) return in->work[i].recipe;
    }
    return 0;
}
#endif
#endif
