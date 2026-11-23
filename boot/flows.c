/* A flow maps one installed command over a source glob, passes its captured
   rows to a reducer, and checks both files with a separately compiled ELF. */
struct flow_spec {
    char path[128],name[32],purpose[320],map[256],args[256],reduce[32];
    char tests[128],implement[768],verify[128];
    u8 digest[32];
};
static int flow_line(const char **cursor,const char *key,char *out,unsigned cap) {
    const char *p=*cursor; unsigned k=strlen(key);
    if (strncmp(p,key,k) || p[k]!=' ') return 0;
    p+=k+1; const char *end=strchr(p,'\n');
    if (!end || end==p || (unsigned)(end-p)>=cap) return 0;
    memcpy(out,p,end-p); out[end-p]=0; *cursor=end+1;
    return one_line(out);
}
static int path_ends(const char *s,const char *suffix) {
    unsigned n=strlen(s),k=strlen(suffix);
    return n>k && !strcmp(s+n-k,suffix);
}
static int flow_input(const char *s,const char *suffix) {
    return (!strncmp(s,"/src/",5) || !strncmp(s,"/work/",6)) && path_ends(s,suffix) && !strchr(s,' ');
}
static int parse_flow(const char *path,struct flow_spec *f) {
    unsigned n; const char *p=source_file(path,&n);
    if (!p || strlen(path)>=sizeof f->path || !flow_input(path,".flow")) return 0;
    sha(p,n,f->digest); strcpy(f->path,path);
    if (!flow_line(&p,"name",f->name,sizeof f->name) || !pkg_name(f->name) ||
        !flow_line(&p,"purpose",f->purpose,sizeof f->purpose) ||
        !flow_line(&p,"map",f->map,sizeof f->map) ||
        !flow_line(&p,"reduce",f->reduce,sizeof f->reduce) || !pkg_name(f->reduce) ||
        !flow_line(&p,"tests",f->tests,sizeof f->tests) || !flow_input(f->tests,".tests") ||
        !flow_line(&p,"implement",f->implement,sizeof f->implement) ||
        !flow_line(&p,"verify",f->verify,sizeof f->verify) || !flow_input(f->verify,".build") || *p) return 0;
    char *args=strchr(f->map,' '); if (!args || !args[1]) return 0;
    *args++=0; strcpy(f->args,args);
    char *glob=strrchr(f->args,' '); glob=glob?glob+1:f->args;
    return pkg_name(f->map) && strcmp(f->map,f->reduce) && !strncmp(glob,"/src/",5) && strstr(glob,"/*");
}
/* Iterate over distribution manifests followed by explicitly installed files. */
static int next_flow(unsigned *cursor,struct flow_spec *f) {
    int dir=source_lookup(0,"/src/flows");
    while (*cursor<sources->count+8) {
        unsigned i=(*cursor)++; char path[128];
        if (i<sources->count) {
            if (sources->entries[i].parent!=(unsigned)dir || source_isdir(i)) continue;
            const char *leaf=source_at(sources->entries[i].name);
            if (!path_ends(leaf,".flow") || strlen(leaf)+12>=sizeof path) continue;
            snprintf(path,sizeof path,"/src/flows/%s",leaf);
        } else {
            i-=sources->count;
            if (!workspace.files[i].installed) continue;
            snprintf(path,sizeof path,"/work/%s",workspace.files[i].name);
        }
        if (parse_flow(path,f)) return 1;
    }
    return 0;
}
static int find_flow(const char *name,struct flow_spec *f) {
    unsigned cursor=0;
    while (next_flow(&cursor,f)) if (!strcmp(f->name,name)) return 1;
    return 0;
}
static int flow_catalog(char *out,unsigned cap,int eligible) {
    unsigned used=0,cursor=0; struct flow_spec f; out[0]=0;
    while (next_flow(&cursor,&f)) {
        if (eligible>0 && app_index(f.map)<0) continue;
        int n=snprintf(out+used,cap-used,"FLOW %s: %s\n",f.name,f.purpose);
        if (n<0 || (unsigned)n>=cap-used) return 0;
        used+=n;
    }
    return 1;
}
static void install_flow(const char *path,unsigned sectors) {
    int i=work_index(path); struct flow_spec f,other; unsigned bytes;
    if (i<0 || !parse_flow(path,&f) || !source_file(f.tests,&bytes) || !bytes ||
        bytes>=sizeof pending.source.tests) { puts("invalid flow or acceptance suite"); return; }
    if (workspace.files[i].installed) { puts("flow already installed"); return; }
    if (find_flow(f.name,&other)) { puts("flow name already registered"); return; }
    if (!compile_app(f.verify)) { puts("flow verifier build failed"); return; }
    next_workspace=workspace; next_workspace.files[i].installed=1;
    if (!save_workspace(sectors)) { puts("flow commit failed"); return; }
    printf("installed flow %s\n",f.name);
}
