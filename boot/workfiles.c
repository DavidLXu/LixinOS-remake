/* Two snapshots in the remaining space of the standard 25 MiB disk. Files
   become visible only after flushing their payload and commit sector. */
#define WORK_BASE (RECEIPT_BASE+APP_COUNT)
#define WORK_BANK 70U
struct work_tree { struct { char name[48],text[4096]; unsigned installed; } files[8]; };
static struct work_tree workspace, next_workspace;
static unsigned work_revision;
struct work_stamp { unsigned magic,revision; u8 payload[32],guard[32]; };
typedef char work_payload_fits[(sizeof workspace<=(WORK_BANK-1)*512)?1:-1];
typedef char user_work_fits[(sizeof(struct tool_input)<=0x30000 &&
    sizeof workspace.files==sizeof(((struct tool_input *)0)->files))?1:-1];
static int work_index(const char *path) {
    if (strncmp(path,"/work/",6) || !path[6]) return -1;
    for (unsigned i=0;i<8;++i) if (!strcmp(path+6,workspace.files[i].name)) return i;
    return -1;
}
static int work_conflict(const char *name) {
    char path[80]; snprintf(path,sizeof path,"/work/%s.c",name);
    if (work_index(path)>=0) return 1;
    snprintf(path,sizeof path,"/work/%s.build",name);
    return work_index(path)>=0;
}
static void load_workspace(unsigned sectors) {
    if (sectors<WORK_BASE+2*WORK_BANK) return;
    for (unsigned bank=0;bank<2;++bank) {
        unsigned base=WORK_BASE+WORK_BANK*bank; struct work_stamp h;
        if (!package_io(0,base,(u8 *)&h,sizeof h) || h.magic!=0x57463136 || (h.revision&1)!=bank ||
            h.revision<=work_revision || !matches(&h,40,h.guard) ||
            !package_io(0,base+1,(u8 *)&next_workspace,sizeof next_workspace) ||
            !matches(&next_workspace,sizeof next_workspace,h.payload)) continue;
        workspace=next_workspace; work_revision=h.revision;
    }
}
static int save_workspace(unsigned sectors) {
    struct work_stamp h={0x57463136,work_revision+1};
    if (!h.revision || sectors<WORK_BASE+2*WORK_BANK) return 0;
    unsigned base=WORK_BASE+WORK_BANK*(h.revision&1);
    sha(&next_workspace,sizeof next_workspace,h.payload); sha(&h,40,h.guard);
    if (!package_io(1,base+1,(u8 *)&next_workspace,sizeof next_workspace) || !package_flush() ||
        !package_io(1,base,(u8 *)&h,sizeof h) || !package_flush()) return 0;
    workspace=next_workspace; work_revision=h.revision; return 1;
}
static void work_put(const char *path,unsigned sectors) {
    if (strncmp(path,"/work/",6) || !path[6] || strlen(path+6)>=48 || machine.abi<7 || !machine.console_line ||
        sectors<WORK_BASE+2*WORK_BANK) { puts("work put /work/FILENAME requires a 25 MiB disk"); return; }
    for (const char *p=path+6;*p;++p)
        if (!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='-' || (*p=='.' && p!=path+6))) {
            puts("invalid workspace filename"); return;
        }
    if (!strcmp(path,"/work/counts.txt") || !strcmp(path,"/work/answer.txt") || !strcmp(path,"/work/scratch.txt") ||
        entry_file(&pending,path)) { puts("workspace path reserved"); return; }
    for (unsigned i=0;i<APP_COUNT;++i) if (entry_file(entries+i,path)) { puts("workspace path reserved"); return; }
    int idx=work_index(path);
    if (idx<0) for (unsigned i=0;i<8;++i) if (!workspace.files[i].name[0]) { idx=i; break; }
    if (idx<0) { puts("workspace full"); return; }
    next_workspace=workspace;
    char *dst=next_workspace.files[idx].text,line[1024]; unsigned used=0; int ok=1;
    puts("enter text; a line containing only . finishes");
    for (;;) {
        if (!machine.console_line(line,sizeof line,0)) { ok=0; continue; }
        if (!strcmp(line,".")) break;
        unsigned n=strlen(line);
        if (n+used+2>sizeof workspace.files[idx].text) { ok=0; continue; }
        memcpy(dst+used,line,n); used+=n; dst[used++]='\n';
    }
    if (!ok) { puts("workspace input too long"); return; }
    dst[used]=0;
    if (workspace.files[idx].name[0]) { puts(!strcmp(workspace.files[idx].text,dst)?"file already saved":"workspace file immutable"); return; }
    strcpy(next_workspace.files[idx].name,path+6);
    puts(save_workspace(sectors)?"workspace file saved":"workspace commit failed");
}
