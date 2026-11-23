/* Four commands, two banks each, in the unused tail of a 25 MiB work disk.
   Data is durable before its checked commit sector becomes visible. */
#include "bearssl.h"
#define APP_COUNT 4
#define APP_BASE 47105U
#define DRAFT_SECTORS 32U
#define APP_SECTORS 480U
#define PACKAGE_MAGIC 0x504b4731U
struct app_source {
    char name[32], usage[256], path[64], build[64], recipe[128];
    char tests[1536], code[8192];
};
struct app_stamp {
    unsigned magic, revision, elf_size, cases;
    unsigned char source_hash[32], elf_hash[32], volume_hash[32], guard[32];
};
struct app_entry { struct app_source source; struct app_stamp stamp; unsigned base; };
static struct app_entry entries[APP_COUNT], pending;
struct result_files { char counts[8192], answer[320]; };
static struct result_files committed, working;
static const struct result_files *visible_result=&committed;
struct result_stamp {
    unsigned revision, magic;
    u8 contents[32], producer[32], reducer[32], verifier[32], volume[32], guard[32];
};
static struct result_stamp latest_result;
static char scratch[8192];
static int has_scratch;
static int save_scratch(const char *data,unsigned size) {
    if (size>=sizeof scratch || memchr(data,0,size)) return 0;
    memcpy(scratch,data,size); scratch[size]=0; has_scratch=1; return 1;
}
#define RESULT_BASE (APP_BASE+2*DRAFT_SECTORS+2*APP_COUNT*APP_SECTORS)
#define RESULT_BANK 20U
#define RECEIPT_BASE (RESULT_BASE+2*RESULT_BANK)
struct compatibility { u8 source[32], elf[32], volume[32], guard[32]; };
static struct compatibility receipts[APP_COUNT];
static int loaded;
static unsigned workspace_writes, launches, volume_hashed;
static unsigned char volume_hash[32];
static int (*package_io)(int,unsigned,u8 *,u32);
static int (*package_flush)(void);
static struct proposal answer;
extern int package_proposal(const char *,const char *,const char *,const char *,int,struct proposal *);
#define SRC_SECTORS ((sizeof(struct app_source)+511)/512)
static void sha(const void *p,unsigned n,unsigned char out[32]) {
    br_sha256_context c; br_sha256_init(&c); br_sha256_update(&c,p,n); br_sha256_out(&c,out);
}
static int matches(const void *p,unsigned n,const unsigned char hash[32]) {
    unsigned char actual[32]; sha(p,n,actual); return !memcmp(actual,hash,32);
}
static void hash_text(const unsigned char hash[32],char dst[65]) {
    const char *digits="0123456789abcdef";
    for (unsigned i=0;i<32;++i) { dst[i*2]=digits[hash[i]>>4]; dst[i*2+1]=digits[hash[i]&15]; }
    dst[64]=0;
}
static unsigned pkg_base(unsigned index,unsigned bank) {
    return index==APP_COUNT?APP_BASE+bank*DRAFT_SECTORS:
        APP_BASE+2*DRAFT_SECTORS+(2*index+bank)*APP_SECTORS;
}
static int pkg_name(const char *s) {
    static const char *reserved[]={"agent","apps","help","ls","cd","cat","read","grep","pwd","run",
        "result","tcc","tool","off","switch","build","rollback","source","replace","note","version"};
    if (!*s || strlen(s)>=sizeof pending.source.name) return 0;
    for (const char *p=s;*p;++p) if (*p<'a' || *p>'z') return 0;
    for (unsigned i=0;i<sizeof reserved/sizeof reserved[0];++i) if (!strcmp(s,reserved[i])) return 0;
    return 1;
}
static void ensure_volume_hash(void) {
    if (!volume_hashed) { sha(sources,sources->bytes,volume_hash); volume_hashed=1; }
}
static int one_line(const char *s) {
    for (;*s;++s) if ((unsigned char)*s<32 || *s==127) return 0;
    return 1;
}
static void read_entry(unsigned index,struct app_entry *e) {
    memset(e,0,sizeof *e);
    for (unsigned bank=0;bank<2;++bank) {
        struct app_stamp h; struct app_source s;
        unsigned base=pkg_base(index,bank);
        if (!package_io(0,base,(u8 *)&h,sizeof h) || h.magic!=PACKAGE_MAGIC ||
            h.revision<=e->stamp.revision || (h.revision&1)!=bank ||
            !matches(&h,(u8 *)h.guard-(u8 *)&h,h.guard) ||
            (index==APP_COUNT ? h.elf_size!=0 : (!h.elf_size || h.elf_size>(APP_SECTORS-1-SRC_SECTORS)*512)) ||
            !package_io(0,base+1,(u8 *)&s,sizeof s) || !matches(&s,sizeof s,h.source_hash)) continue;
        if (index!=APP_COUNT && (!package_io(0,base+1+SRC_SECTORS,tool_image,h.elf_size) ||
            !matches(tool_image,h.elf_size,h.elf_hash))) continue;
        e->source=s; e->stamp=h; e->base=base;
    }
}
static int store_entry(unsigned index,struct app_entry *dest,const struct app_source *src,unsigned size,unsigned cases) {
    struct app_stamp h={0};
    h.magic=PACKAGE_MAGIC; h.revision=dest->stamp.revision+1;
    if (!h.revision || size>(APP_SECTORS-1-SRC_SECTORS)*512) return 0;
    unsigned base=pkg_base(index,h.revision&1);
    h.elf_size=size; h.cases=cases; sha(src,sizeof *src,h.source_hash);
    sha(tool_image,size,h.elf_hash); memcpy(h.volume_hash,volume_hash,32);
    sha(&h,(u8 *)h.guard-(u8 *)&h,h.guard);
    if (!package_io(1,base+1,(u8 *)src,sizeof *src) ||
        (size && !package_io(1,base+1+SRC_SECTORS,tool_image,size)) || !package_flush() ||
        !package_io(1,base,(u8 *)&h,sizeof h) || !package_flush()) return 0;
    dest->source=*src; dest->stamp=h; dest->base=base;
    return 1;
}
static const char *entry_file(const struct app_entry *e,const char *name) {
    if (!e->stamp.revision) return NULL;
    if (!strcmp(name,e->source.path)) return e->source.code;
    if (!strcmp(name,e->source.build)) return e->source.recipe;
    return NULL;
}
static const char *work_file(const char *name) {
    if (strncmp(name,"/work/",6)) return NULL;
    if (!strcmp(name,"/work/scratch.txt") && has_scratch) return scratch;
    if (!strcmp(name,"/work/counts.txt") && visible_result->counts[0]) return visible_result->counts;
    if (!strcmp(name,"/work/answer.txt") && visible_result->answer[0]) return visible_result->answer;
    const char *data=entry_file(&pending,name);
    for (unsigned i=0;i<APP_COUNT && !data;++i) data=entry_file(entries+i,name);
    return data;
}
static void copy_workspace(struct tool_input *in) {
    in->has_scratch=has_scratch; strcpy(in->scratch,scratch);
    strcpy(in->counts,visible_result->counts); strcpy(in->answer,visible_result->answer);
    for (unsigned i=0;i<=APP_COUNT;++i) {
        const struct app_entry *e=i==APP_COUNT ? &pending : entries+i;
        if (!e->stamp.revision) continue;
        if (i!=APP_COUNT && pending.stamp.revision && !strcmp(e->source.name,pending.source.name)) continue;
        unsigned n=in->work_count++;
        strcpy(in->work[n].name,e->source.name);
        strcpy(in->work[n].code,e->source.code);
        strcpy(in->work[n].recipe,e->source.recipe);
    }
}
static int app_index(const char *name) {
    for (unsigned i=0;i<APP_COUNT;++i) if (entries[i].stamp.revision && !strcmp(entries[i].source.name,name)) return i;
    return -1;
}
static int catalog(char *buf,unsigned cap) {
    unsigned used=0; buf[0]=0;
    for (unsigned i=0;i<APP_COUNT;++i) {
        if (!entries[i].stamp.revision) continue;
        int n=snprintf(buf+used,cap-used,"%s %s\n",entries[i].source.name,entries[i].source.usage);
        if (n<0 || (unsigned)n>=cap-used) return 0;
        used+=n;
    }
    return used;
}
static unsigned check_cases(const char *suite,const char *tools) {
    char checks[sizeof pending.source.tests]; strcpy(checks,suite);
    char *line=checks; unsigned cases=0;
    while (*line) {
        char *end=strchr(line,'\n'); if (end) *end=0;
        char *expected=strchr(line,'|');
        if (!expected) { puts("invalid acceptance case"); return 0; }
        *expected++=0;
        const char *output=NULL; unsigned n=0; int cwd;
        int result=tool_run(line,machine.source,tools,0,&output,&n,&cwd);
        unsigned len=strlen(expected);
        if (result || n!=len+1 || memcmp(output,expected,len) || output[len]!='\n') {
            snprintf(machine.diagnostic,sizeof machine.diagnostic,"test failed: %s expected %s (exit %d, %.160s)",line,expected,result,output?output:"");
            puts(machine.diagnostic); return 0;
        }
        ++cases; if (!end) break; line=end+1;
    }
    return cases;
}
static int verify_draft(const char *tools) {
    struct app_source *s=&pending.source;
    if (!pending.stamp.revision) { puts("no saved draft"); return 0; }
    int index=app_index(s->name);
    if (index<0) for (unsigned i=0;i<APP_COUNT;++i) if (!entries[i].stamp.revision) { index=i; break; }
    if (index<0) { puts("tool catalog full"); return 0; }
    if (entries[index].stamp.revision &&
        !memcmp(pending.stamp.source_hash,entries[index].stamp.source_hash,32) &&
        !memcmp(volume_hash,entries[index].stamp.volume_hash,32)) {
        puts("draft already installed"); return 1;
    }
    if (!compile_app(s->build)) { puts("application build failed"); return 0; }
    unsigned cases=check_cases(s->tests,tools);
    if (!cases) return 0;
    if (!store_entry(index,entries+index,s,tool_size,cases)) { puts("tool install failed"); return 0; }
    char hex[65]; hash_text(entries[index].stamp.elf_hash,hex);
    printf("installed %s tests=%u elf=%s\n",s->name,cases,hex);
    return 1;
}
static char *argument(char **tail) {
    while (**tail==' ') ++*tail;
    char *start=*tail;
    while (**tail && **tail!=' ') ++*tail;
    if (**tail) *(*tail)++=0;
    while (**tail==' ') ++*tail;
    return start;
}
static int make_app(char *args,const char *url,const char *model,const char *tools) {
    char *name=argument(&args),*tests=argument(&args);
    unsigned n; const char *suite=source_file(tests,&n);
    if (!pkg_name(name) || strncmp(tests,"/src/",5) || !*args || !suite || !n || n>=sizeof pending.source.tests) {
        puts("agent make NAME /src/FILE.tests GOAL"); return 0;
    }
    static char context[12288];
    unsigned bytes; const char *sdk=source_file("/src/user/app.h",&bytes);
    if (!sdk) return 0;
    struct app_source draft={0};
    strcpy(draft.name,name); strcpy(draft.tests,suite);
    snprintf(draft.path,sizeof draft.path,"/work/%s.c",name);
    snprintf(draft.build,sizeof draft.build,"/work/%s.build",name);
    snprintf(draft.recipe,sizeof draft.recipe,"/src/user/crt.c\n%s\n",draft.path);
    machine.diagnostic[0]=0;
    for (unsigned attempt=0;attempt<3;++attempt) {
        snprintf(context,sizeof context,"Name: %s\nSDK:\n%s\nAcceptance:\n%s\nPrevious source:\n%s\nDiagnostics:\n%s",
                 name,sdk,suite,draft.code,machine.diagnostic);
        if (!package_proposal(url,model,args,context,1,&answer)) {
            puts(answer.message);
            if (strncmp(answer.message,"invalid package response",24)) return 0;
            snprintf(machine.diagnostic,sizeof machine.diagnostic,"%.512s. Return a concise JSON object with source and usage.",answer.message);
            continue;
        }
        if (!one_line(answer.expected)) { puts("invalid tool usage"); return 0; }
        strcpy(draft.code,answer.after); strcpy(draft.usage,answer.expected);
        if (!store_entry(APP_COUNT,&pending,&draft,0,0)) { puts("draft save failed"); return 0; }
        ++workspace_writes;
        if (verify_draft(tools)) return 1;
    }
    return 0;
}
struct capture { char *text; unsigned used,cap; };
static int emit_output(struct capture *to,const char *data,unsigned n) {
    if (!to) { _write(1,data,n); return 1; }
    if (n>=to->cap-to->used || memchr(data,0,n)) { puts("result too large or contains NUL"); return 0; }
    memcpy(to->text+to->used,data,n); to->used+=n; to->text[to->used]=0;
    return 1;
}
static int execute_one(const char *args,const char *tools,struct capture *to) {
    const char *text; unsigned size; int cwd;
    ++launches;
    int err=tool_run(args,machine.source,tools,machine.cwd,&text,&size,&cwd);
    if (err) { printf("application failed (%d)\n",err); return 0; }
    return emit_output(to,text,size);
}
static int prepare_installed(unsigned index) {
    struct app_entry *e=entries+index;
    tool_size=e->stamp.elf_size;
    if (!package_io(0,e->base+1+SRC_SECTORS,tool_image,tool_size) ||
        !matches(tool_image,tool_size,e->stamp.elf_hash) ||
        !load_app(e->source.name)) { puts("installed ELF invalid"); return 0; }
    return 1;
}
static int execute_app(unsigned index,char *args,const char *tools,int audit,struct capture *to) {
    struct app_entry *e=entries+index;
    struct compatibility *r=receipts+index;
    ensure_volume_hash();
    if (memcmp(volume_hash,e->stamp.volume_hash,32) && !(matches(r,96,r->guard) &&
        !memcmp(r->volume,volume_hash,32) && !memcmp(r->elf,e->stamp.elf_hash,32) &&
        !memcmp(r->source,e->stamp.source_hash,32))) {
        puts("source volume changed; rebuild and verify tool"); return 0;
    }
    if (!prepare_installed(index)) return 0;
    if (audit) {
        char hex[65]; hash_text(e->stamp.elf_hash,hex);
        printf("invoke %s elf=%s\n",e->source.name,hex);
    }
    char *star=strchr(args,'*');
    if (!star) return execute_one(args,tools,to);
    char *path=star;
    while (path>args && path[-1]!=' ') --path;
    if (*path!='/' || star==path || star[-1]!='/' || strchr(star+1,'/') || strchr(star+1,' ') || strchr(star+1,'*')) {
        puts("expected final absolute directory glob"); return 0;
    }
    char suffix[128],folder[256],line[2048]; unsigned prefix=path-args,dlen=star-path;
    if (dlen>=sizeof folder || strlen(star+1)>=sizeof suffix) return 0;
    strcpy(suffix,star+1); memcpy(folder,path,dlen); folder[dlen]=0;
    int dir=source_lookup(0,folder); unsigned hits=0;
    for (unsigned i=1;dir>=0 && i<sources->count;++i) {
        if (sources->entries[i].parent!=(unsigned)dir || source_isdir(i)) continue;
        const char *name=source_at(sources->entries[i].name);
        unsigned n=strlen(name),s=strlen(suffix);
        if (n<s || strcmp(name+n-s,suffix)) continue;
        if (snprintf(line,sizeof line,"%.*s%s%s",(int)prefix,args,folder,name)>=(int)sizeof line) { puts("expanded path too long"); return 0; }
        if (!emit_output(to,folder,dlen) || !emit_output(to,name,n) || !emit_output(to," ",1) ||
            !execute_one(line,tools,to)) return 0;
        ++hits;
    }
    if (!hits) puts("no matching files");
    return hits!=0;
}
#include "composition.c"
static int packages(const char *cmd,const char *url,const char *model,const char *tools,
                    int (*disk_io)(int,unsigned,u8 *,u32),int (*flush)(void),unsigned sectors) {
    package_io=disk_io; package_flush=flush;
    if (sectors<APP_BASE+2*DRAFT_SECTORS+2*APP_COUNT*APP_SECTORS) {
        if (!strncmp(cmd,"agent make ",11) || !strncmp(cmd,"agent task ",11)) { puts("package workspace requires a 25 MiB disk"); return 1; }
        return 0;
    }
    if (!loaded) {
        for (unsigned i=0;i<APP_COUNT;++i) read_entry(i,entries+i);
        read_entry(APP_COUNT,&pending); load_results(sectors); loaded=1;
    }
    if (!strcmp(cmd,"apps") || !strcmp(cmd,"help")) {
        char list[1400]; catalog(list,sizeof list);
        if (!strcmp(cmd,"help")) puts("version source replace build switch rollback note pwd cd ls cat read grep tcc run tool agent apps off");
        printf("installed commands:\n%s",list); return 1;
    }
    if (!strncmp(cmd,"apps info ",10)) {
        int index=app_index(cmd+10);
        if (index<0) { puts("unknown installed command"); return 1; }
        const struct app_entry *e=entries+index;
        char source[65],elf[65],volume[65];
        hash_text(e->stamp.source_hash,source); hash_text(e->stamp.elf_hash,elf);
        hash_text(e->stamp.volume_hash,volume);
        printf("%s %s\nrevision=%u tests=%u\ninputs=%s\nvolume=%s\nelf=%s\n%s",
            e->source.name,e->source.usage,e->stamp.revision,e->stamp.cases,source,volume,elf,e->source.tests);
        return 1;
    }
    if (!strncmp(cmd,"apps recheck ",13)) { recheck_installed(cmd+13,tools,sectors); return 2; }
    if (!strcmp(cmd,"result info")) { result_info(); return 1; }
    if (!strcmp(cmd,"result verify") || !strncmp(cmd,"result verify ",14)) {
        const char *args=cmd[13]?cmd+14:"/work/counts.txt /work/answer.txt";
        u8 verifier[32]; verify_result(args,tools,verifier); return 2;
    }
    if (!strcmp(cmd,"apps stats")) {
        printf("application_builds=%u source_writes=%u executions=%u\n",app_builds,workspace_writes,launches); return 1;
    }
    char line[2048];
    if (strlen(cmd)>=sizeof line) return 0;
    strcpy(line,cmd);
    if (!strncmp(cmd,"agent make ",11) || !strcmp(cmd,"apps resume")) {
        ensure_volume_hash();
        if (!strcmp(cmd,"apps resume")) verify_draft(tools);
        else make_app(line+11,url,model,tools);
        return 2;
    }
    int choose=!strncmp(cmd,"agent task ",11);
    int audit=choose || !strncmp(cmd,"agent use ",10);
    if (audit) {
        char list[1400],*goal=line+(choose?11:10),*suite=NULL;
        if (choose && !strncmp(goal,"/src/",5)) suite=argument(&goal);
        int count=catalog(list,sizeof list);
        printf("discovered:\n%s",count?list:"(empty)\n");
        if (!count && !choose) return 1;
        int result=package_proposal(url,model,goal,count?list:"(empty)",choose?2:0,&answer);
        if (!result) { puts(answer.message); return 1; }
        if (result==3) {
            if (suite || sectors<RECEIPT_BASE+APP_COUNT) { puts("composition unavailable"); return 1; }
            compose_longest(answer.command,answer.expected,url,model,tools);
            return 2;
        }
        if (result==2) {
            if (!pkg_name(answer.read) || app_index(answer.read)>=0) { puts("invalid new tool selection"); return 1; }
            printf("missing capability: %s\n",answer.read);
            if (!suite) { puts("supply an acceptance suite: agent task /src/FILE.tests GOAL"); return 1; }
            char args[2048];
            if (snprintf(args,sizeof args,"%s %s %s",answer.read,suite,goal)>=(int)sizeof args) { puts("task too long"); return 1; }
            ensure_volume_hash();
            make_app(args,url,model,tools);
            return 2;
        }
        strcpy(line,answer.command);
    }
    char *args=line,*name=argument(&args); int index=app_index(name);
    if (index<0) { if (audit) puts("selected command is not installed"); return audit; }
    execute_app(index,args,tools,audit,NULL); return 2;
}
