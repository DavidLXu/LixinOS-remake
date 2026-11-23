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
static int work_name(const char *p,const char *q) {
    while (*p && *p==*q) { ++p; ++q; }
    return !*p && !*q;
}
/* Build an absolute path for SDK calls, including a shell cwd below /work. */
static int user_base_path(int node,char *out,unsigned cap,unsigned *used) {
    if(node<0 || (unsigned)node>=sources->count)return 0;
    if(!node){out[0]='/';out[1]=0;*used=1;return 1;}
    if(!user_base_path(sources->entries[node].parent,out,cap,used))return 0;
    const char *name=source_at(sources->entries[node].name);
    if(*used>1){if(*used+2>cap)return 0;out[(*used)++]='/';}
    while(*name){if(*used+2>cap)return 0;out[(*used)++]=*name++;}
    out[*used]=0;return 1;
}
static int user_path(const struct tool_input *in,const char *path,char *out,unsigned cap) {
    unsigned n=0;
    if(!*path||cap<2)return 0;
    if(*path=='/'){out[n++]='/';out[n]=0;}
    else if(in->shell_dir[0]){
        while(in->shell_dir[n]){if(n+2>cap)return 0;out[n]=in->shell_dir[n];++n;}out[n]=0;
    }else if(!user_base_path(in->cwd,out,cap,&n))return 0;
    while(*path){
        while(*path=='/')++path;
        const char *part=path;while(*path && *path!='/')++path;unsigned len=path-part;
        if(!len||(len==1 && *part=='.'))continue;
        if(len==2 && part[0]=='.' && part[1]=='.'){
            while(n>1 && out[n-1]!='/')--n;if(n>1)--n;out[n]=0;continue;
        }
        if(n+len+2>cap)return 0;
        if(n>1)out[n++]='/';for(unsigned i=0;i<len;++i)out[n++]=part[i];out[n]=0;
    }return 1;
}
static const char *user_work_file(const struct tool_input *in,const char *path) {
    char absolute[256];
    if(!user_path(in,path,absolute,sizeof absolute))return 0;
    const char *prefix="/work/",*leaf=absolute;
    while(*prefix && *prefix==*leaf){++prefix;++leaf;}if(*prefix||!*leaf)return 0;
    if(work_name(leaf,"dsh.build") && USER_APPLETS->recipe[0])return USER_APPLETS->recipe;
    if(leaf[0]=='a' && leaf[1]=='p' && leaf[2]=='p' && leaf[3]=='l' && leaf[4]=='e' && leaf[5]=='t' && leaf[6]=='-'){
        for(unsigned i=0;i<APPLET_COUNT;++i){
            const struct applet_source *e=USER_APPLETS->entries+i;const char *g=e->group,*p=leaf+7;
            if(!*g)continue;
            while(*g && *g==*p){++g;++p;}
            if(!*g && work_name(p,".c"))return e->code;
        }
    }
    if (in->counts[0] && work_name(leaf,"counts.txt")) return in->counts;
    if (in->answer[0] && work_name(leaf,"answer.txt")) return in->answer;
    if (in->has_scratch && work_name(leaf,"scratch.txt")) return in->scratch;
    for (unsigned i=0;i<VISIBLE_WORK_FILES;++i) if (in->files[i].name[0] && in->files[i].installed!=3 && work_name(leaf,in->files[i].name)) return in->files[i].text;
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
