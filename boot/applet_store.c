/* Per-module guest sources, independently journaled after both shell snapshots.
   The immutable release suite and guest-local suite have separate build recipes. */
#define MODULE_BASE (SHELL_STORE_BASE+2*SHELL_STORE_BANK)
#define MODULE_COUNT APPLET_COUNT
#define MODULE_BANK 18U
struct module_record { unsigned magic,revision; u8 payload[32],guard[32]; };
typedef char module_fits[(sizeof(struct applet_source)<=(MODULE_BANK-1)*512 &&
    MODULE_BASE+2*MODULE_COUNT*MODULE_BANK<=2048+16384)?1:-1];
static struct applet_source modules[MODULE_COUNT],draft_module;
static unsigned module_revisions[MODULE_COUNT];
static int use_draft;
static int module_slot(const char *g){
    for(int i=0;i<MODULE_COUNT;++i)if(modules[i].group[0] && !strcmp(g,modules[i].group))return i;
    return -1;
}
static void load_modules(unsigned sectors){
    if(sectors<MODULE_BASE+2*MODULE_COUNT*MODULE_BANK)return;
    for(int i=0;i<MODULE_COUNT;++i)for(unsigned bank=0;bank<2;++bank){
        unsigned base=MODULE_BASE+(i*2+bank)*MODULE_BANK;struct module_record h;
        if(!package_io(0,base,(u8 *)&h,sizeof h)||h.magic!=0x44534d32||
           (h.revision&1)!=bank||h.revision<=module_revisions[i]||!matches(&h,40,h.guard)||
           !package_io(0,base+1,(u8 *)&draft_module,sizeof draft_module)||
           !matches(&draft_module,sizeof draft_module,h.payload)||
           !memchr(draft_module.group,0,sizeof draft_module.group)||!memchr(draft_module.code,0,sizeof draft_module.code)||
           !memchr(draft_module.usage,0,sizeof draft_module.usage))continue;
        modules[i]=draft_module;module_revisions[i]=h.revision;
    }
}
static int save_module(unsigned sectors){
    if(sectors<MODULE_BASE+2*MODULE_COUNT*MODULE_BANK)return 0;
    int i=module_slot(draft_module.group);
    if(i<0)for(int j=0;j<MODULE_COUNT;++j)if(!modules[j].group[0]){i=j;break;}
    if(i<0 || module_revisions[i]==~0U)return 0;
    struct module_record h={0x44534d32,module_revisions[i]+1};
    sha(&draft_module,sizeof draft_module,h.payload);sha(&h,40,h.guard);
    unsigned base=MODULE_BASE+(i*2+(h.revision&1))*MODULE_BANK;
    if(!package_io(1,base+1,(u8 *)&draft_module,sizeof draft_module)||!package_flush()||
       !package_io(1,base,(u8 *)&h,sizeof h)||!package_flush())return 0;
    modules[i]=draft_module;module_revisions[i]=h.revision;return 1;
}
static int module_path(const char *path){return !strcmp(path,"/work/dsh.build")||!strncmp(path,"/work/applet-",13);}
static const char *module_source(const char *group){
    if(use_draft&&!strcmp(group,draft_module.group))return draft_module.code;
    int idx=module_slot(group);return idx<0?NULL:modules[idx].code;
}
static const char *module_file(const char *path){
    if(!strcmp(path,"/work/dsh.build")){
        int node=source_lookup(0,"/src/user/deepseek.build");if(node<0)return NULL;
        const char *p=source_at(sources->entries[node].offset);
        static char recipe[4096];unsigned used=0;recipe[0]=0;
        while(*p){
            const char *end=strchr(p,'\n');char name[192];
            if(!end || end-p>=sizeof name)return NULL;
            memcpy(name,p,end-p);name[end-p]=0;p=end+1;
            if(!strncmp(name,"/src/user/deepseek/",19)){
                const char *g=name+19;char group[32];unsigned len=strlen(g);
                if(len>2 && len-2<sizeof group && !strcmp(g+len-2,".c")){
                    memcpy(group,g,len-2);group[len-2]=0;
                    if(module_source(group))snprintf(name,sizeof name,"/work/applet-%s.c",group);
                }
            }
            int n=snprintf(recipe+used,sizeof recipe-used,"%s\n",name);
            if(n<0||(unsigned)n>=sizeof recipe-used)return NULL;used+=n;
        }
        return recipe;
    }
    const char *g=path+13;unsigned n=strlen(g);char group[32];
    if(n<=2||n-2>=sizeof group||strcmp(g+n-2,".c"))return NULL;
    memcpy(group,g,n-2);group[n-2]=0;return module_source(group);
}
