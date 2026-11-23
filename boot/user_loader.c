/* Private foreground process arena with 4 KiB stage-1 mappings. */
#define USER_BASE 0x46000000UL
#define PAGE 4096
#define PRIVATE_PAGE (0x60000000000707UL)
#define USER_RO (0x600000000007c7UL)
#define USER_RW (0x60000000000747UL)
#define USER_RX (0x200000000007c7UL)
extern u64 user_pages[512];
extern const char tool_source[];
extern int enter_user(u64,struct tool_input *,struct tool_output *);
static u64 permissions[512];
static int tool_ready;

static void flush_pages(void) {
    __asm__ volatile("dsb ishst\ntlbi vmalle1\ndsb ish\nisb" ::: "memory");
}
static void set_user_pages(int enable) {
    /* Break before make. Page tables and this stack are outside the arena. */
    memset(user_pages,0,512*sizeof(u64));
    flush_pages();
    for (unsigned i=0; i<512; ++i)
        user_pages[i]=(USER_BASE+i*PAGE) | (enable && permissions[i]?permissions[i]:PRIVATE_PAGE);
    flush_pages();
}
static void permit_data(void *base, unsigned bytes) {
    unsigned i=((u64)base-USER_BASE)/PAGE;
    for (unsigned n=0;n<bytes;n+=PAGE) permissions[i++]=USER_RW;
}
static int tool_compile(const char *source) {
    tool_ready=0;
    if (!compile_source(source,0,1) || !valid_range(tool_image,tool_size,USER_BASE,USER_BASE+0x100000)) return 0;
    const struct elf_header *h=(const void *)tool_image;
    const struct program_header *ph=(const void *)(tool_image+h->phoff);
    memset(permissions,0,sizeof permissions);
    for (unsigned i=0;i<h->count;++i) {
        const struct program_header *p=ph+i;
        if (p->type!=1 || !p->mem) continue;
        if ((p->flags&3)==3) return 0;
        u64 flags=p->flags&1?USER_RX:p->flags&2?USER_RW:USER_RO;
        unsigned first=(p->va-USER_BASE)/PAGE, last=(p->va+p->mem-1-USER_BASE)/PAGE;
        for (unsigned j=first;j<=last;++j) {
            if (permissions[j] && permissions[j]!=flags) return 0;
            permissions[j]=flags;
        }
    }
    tool_ready=1;
    return 1;
}
static int tool_run(const char *line,const char *kernel,const char *tools,int cwd,
                    const char **text,unsigned *size,int *next_cwd) {
    if (!tool_ready) return -1;
    struct tool_input *in=(void *)(USER_BASE+0x110000);
    struct tool_output *out=(void *)(USER_BASE+0x140000);
    if (strlen(line)>=sizeof in->command || strlen(kernel)>=sizeof in->kernel ||
        strlen(tools)>=sizeof in->tools) return -1;
    memset((void *)USER_BASE,0,0x200000);
    const struct elf_header *h=(const void *)tool_image;
    const struct program_header *ph=(const void *)(tool_image+h->phoff);
    for (unsigned i=0;i<h->count;++i)
        if (ph[i].type==1) memcpy((void *)ph[i].va,tool_image+ph[i].offset,ph[i].file);
    in->cwd=out->cwd=cwd;
    strcpy(in->command,line); strcpy(in->kernel,kernel); strcpy(in->tools,tools);
    permit_data(in,sizeof *in); permit_data(out,sizeof *out);
    permit_data((void *)(USER_BASE+0x1f0000),0x10000);
    set_user_pages(1);
    __asm__ volatile("ic iallu\ndsb sy\nisb" ::: "memory");
    if (!trial_armed) arm_timer(2);
    int result=enter_user(h->entry,in,out);
    if (!trial_armed) end_trial();
    set_user_pages(0);
    if (result || out->length>sizeof out->text || (out->cwd!=0 && out->cwd!=1))
        return result==2 ? -2 : -1;
    if (out->truncated) return -3;
    *size=out->length; *text=out->text; *next_cwd=out->cwd;
    return 0;
}
