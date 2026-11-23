/* Publish the two verified result files and their ELF identities together. */
static void load_results(unsigned sectors) {
    if (sectors<RECEIPT_BASE+APP_COUNT) return;
    for (unsigned i=0;i<APP_COUNT;++i)
        if (!package_io(0,RECEIPT_BASE+i,(u8 *)(receipts+i),sizeof receipts[i])) memset(receipts+i,0,sizeof receipts[i]);
    for (unsigned bank=0;bank<2;++bank) {
        unsigned base=RESULT_BASE+RESULT_BANK*bank;
        struct result_stamp h;
        if (!package_io(0,base,(u8 *)&h,sizeof h) || h.magic!=0x52313530 ||
            h.revision<=latest_result.revision || (h.revision&1)!=bank ||
            !matches(&h,(u8 *)h.guard-(u8 *)&h,h.guard) ||
            !package_io(0,base+1,(u8 *)&working,sizeof working) || !matches(&working,sizeof working,h.contents) ||
            !memchr(working.counts,0,sizeof working.counts) || !memchr(working.answer,0,sizeof working.answer)) continue;
        committed=working; latest_result=h;
    }
}
static void recheck_installed(const char *name,const char *tools,unsigned sectors) {
    int index=app_index(name);
    if (index<0 || sectors<RECEIPT_BASE+APP_COUNT || !prepare_installed(index)) { puts("cannot recheck tool"); return; }
    struct app_entry *e=entries+index;
    unsigned cases=check_cases(e->source.tests,tools);
    if (!cases || cases!=e->stamp.cases) return;
    struct compatibility r; ensure_volume_hash();
    memcpy(r.volume,volume_hash,32); memcpy(r.elf,e->stamp.elf_hash,32); memcpy(r.source,e->stamp.source_hash,32);
    sha(&r,96,r.guard);
    if (!package_io(1,RECEIPT_BASE+index,(u8 *)&r,sizeof r) || !package_flush()) { puts("recheck save failed"); return; }
    receipts[index]=r;
    char hex[65]; hash_text(r.elf,hex);
    printf("rechecked %s tests=%u elf=%s\n",name,cases,hex);
}
static int verify_result(const char *args,const char *tools,u8 hash[32]) {
    if (!compile_app("/src/validation/longest.build")) { puts("verifier build failed"); return 0; }
    sha(tool_image,tool_size,hash);
    const char *text=NULL; unsigned n=0; int cwd;
    int err=tool_run(args,machine.source,tools,0,&text,&n,&cwd);
    if (text) _write(1,text,n);
    if (err) { puts("verification rejected"); return 0; }
    return 1;
}
static void result_info(void) {
    if (!latest_result.revision) { puts("no verified result"); return; }
    char p[65],r[65],v[65],files[65];
    hash_text(latest_result.producer,p); hash_text(latest_result.reducer,r);
    hash_text(latest_result.verifier,v); hash_text(latest_result.contents,files);
    printf("revision=%u producer=%s\nreducer=%s\nverifier=%s\nfiles=%s\n",latest_result.revision,p,r,v,files);
}
static int publish_result(struct result_stamp *h) {
    h->revision=latest_result.revision+1;
    if (!h->revision) return 0;
    h->magic=0x52313530; sha(&working,sizeof working,h->contents);
    memcpy(h->volume,volume_hash,32);
    sha(h,(u8 *)h->guard-(u8 *)h,h->guard);
    unsigned base=RESULT_BASE+(h->revision&1)*RESULT_BANK;
    if (!package_io(1,base+1,(u8 *)&working,sizeof working) || !package_flush() ||
        !package_io(1,base,(u8 *)h,sizeof *h) || !package_flush()) return 0;
    latest_result=*h; committed=working;
    return 1;
}
static void compose_longest(const char *producer,const char *reducer,const char *url,const char *model,const char *tools) {
    int counter=app_index(producer);
    if (counter<0 || strcmp(reducer,"max") || !strcmp(producer,reducer)) { puts("invalid composition"); return; }
    memset(&working,0,sizeof working);
    struct capture rows={working.counts,0,sizeof working.counts};
    char inputs[]="-l /src/user/*.c";
    if (!execute_app(counter,inputs,tools,1,&rows)) return;
    int reduce=app_index("max");
    if (reduce<0) {
        puts("missing capability: max");
        char task[]="max /src/validation/max.tests Read exactly one text file argument. Each line is PATH SPACE DECIMAL_COUNT. "
            "Print the winning path, one space, its count and a newline. Choose the largest integer, then lexicographically smallest path for ties. "
            "Inputs are at most 8191 bytes. Use one 8192-byte static buffer and parse it in place. "
            "Keep only the current best row; avoid large arrays and allocation. No libc, directory scanning or sorting. "
            "Use file_open/file_read/file_close. Output exactly one line: putstr(winner_path), putchar(32), "
            "print the decimal count, then putchar(10). puts appends a newline after its argument. Publish compact C under 8192 bytes.";
        if (!make_app(task,url,model,tools)) return;
        reduce=app_index("max");
    }
    if (reduce<0) return;
    struct result_stamp h={0};
    memcpy(h.producer,entries[counter].stamp.elf_hash,32); memcpy(h.reducer,entries[reduce].stamp.elf_hash,32);
    struct capture answer_file={working.answer,0,sizeof working.answer};
    char arg[]="/work/counts.txt";
    visible_result=&working;
    int ok=execute_app(reduce,arg,tools,1,&answer_file) &&
        verify_result("/work/counts.txt /work/answer.txt",tools,h.verifier);
    visible_result=&committed;
    if (!ok) return;
    if (!publish_result(&h)) { puts("result commit failed"); return; }
    printf("verified result: %s",committed.answer);
}
