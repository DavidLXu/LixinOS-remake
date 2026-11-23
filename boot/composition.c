/* Publish the two verified result files and their ELF identities together. */
static void load_results(unsigned sectors) {
    if (sectors<RECEIPT_BASE+APP_COUNT) return;
    for (unsigned i=0;i<APP_COUNT;++i)
        if (!package_io(0,RECEIPT_BASE+i,(u8 *)(receipts+i),sizeof receipts[i])) memset(receipts+i,0,sizeof receipts[i]);
    for (unsigned bank=0;bank<2;++bank) {
        unsigned base=RESULT_BASE+RESULT_BANK*bank;
        struct result_stamp h;
        if (!package_io(0,base,(u8 *)&h,sizeof h) ||
            h.revision<=latest_result.revision || (h.revision&1)!=bank) continue;
        if (h.magic==0x52313530) {
            /* 1.5 stored its guard immediately after volume; it had one verifier. */
            if (!matches(&h,(u8 *)h.flow-(u8 *)&h,(u8 *)h.flow)) continue;
            strcpy(h.flow,"/src/flows/longest.flow");
        } else if (h.magic!=0x52313630 || !matches(&h,(u8 *)h.guard-(u8 *)&h,h.guard) ||
                   !memchr(h.flow,0,sizeof h.flow)) continue;
        if (!package_io(0,base+1,(u8 *)&working,sizeof working) || !matches(&working,sizeof working,h.contents) ||
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
static int verify_result(const struct flow_spec *f,const char *args,const char *tools,u8 hash[32],const u8 *expected) {
    if (!compile_app(f->verify)) { puts("verifier build failed"); return 0; }
    sha(tool_image,tool_size,hash);
    if (expected && memcmp(hash,expected,32)) { puts("verifier ELF changed"); return 0; }
    const char *text=NULL; unsigned n=0; int cwd;
    int err=tool_run(args,machine.source,tools,0,&text,&n,&cwd);
    if (text) _write(1,text,n);
    if (err) { puts("verification rejected"); return 0; }
    return 1;
}
static void verify_committed(const char *args,const char *tools) {
    struct flow_spec f; u8 hash[32];
    if (!latest_result.revision || !parse_flow(latest_result.flow,&f)) { puts("no available result manifest"); return; }
    int legacy=latest_result.magic==0x52313530;
    if (!legacy && memcmp(f.digest,latest_result.manifest,32)) { puts("result manifest changed"); return; }
    verify_result(&f,args,tools,hash,legacy?NULL:latest_result.verifier);
}
static void result_info(void) {
    if (!latest_result.revision) { puts("no verified result"); return; }
    char p[65],r[65],v[65],files[65];
    hash_text(latest_result.producer,p); hash_text(latest_result.reducer,r);
    hash_text(latest_result.verifier,v); hash_text(latest_result.contents,files);
    printf("manifest=%s\n",latest_result.flow);
    if (latest_result.magic!=0x52313530) {
        char digest[65]; hash_text(latest_result.manifest,digest);
        printf("manifest_sha256=%s\n",digest);
    }
    printf("revision=%u producer=%s\nreducer=%s\nverifier=%s\nfiles=%s\n",latest_result.revision,p,r,v,files);
}
static int publish_result(struct result_stamp *h) {
    h->revision=latest_result.revision+1;
    if (!h->revision) return 0;
    h->magic=0x52313630; sha(&working,sizeof working,h->contents);
    memcpy(h->volume,volume_hash,32);
    sha(h,(u8 *)h->guard-(u8 *)h,h->guard);
    unsigned base=RESULT_BASE+(h->revision&1)*RESULT_BANK;
    if (!package_io(1,base+1,(u8 *)&working,sizeof working) || !package_flush() ||
        !package_io(1,base,(u8 *)h,sizeof *h) || !package_flush()) return 0;
    latest_result=*h; committed=working;
    return 1;
}
static void run_flow(const char *name,const char *url,const char *model,const char *tools,unsigned sectors) {
    struct flow_spec f;
    if (sectors<RECEIPT_BASE+APP_COUNT || !find_flow(name,&f)) { puts("flow unavailable"); return; }
    int counter=app_index(f.map);
    if (counter<0) { printf("required command missing: %s\n",f.map); return; }
    printf("selected flow %s from %s\n",f.name,f.path);
    memset(&working,0,sizeof working);
    struct capture rows={working.counts,0,sizeof working.counts};
    if (!execute_app(counter,f.args,tools,1,&rows)) return;
    int reduce=app_index(f.reduce);
    if (reduce<0) {
        printf("missing capability: %s\n",f.reduce);
        char task[1024];
        snprintf(task,sizeof task,"%s %s %s",f.reduce,f.tests,f.implement);
        if (!make_app(task,url,model,tools)) return;
        reduce=app_index(f.reduce);
    }
    if (reduce<0) return;
    struct result_stamp h={0};
    strcpy(h.flow,f.path); memcpy(h.manifest,f.digest,32);
    memcpy(h.producer,entries[counter].stamp.elf_hash,32); memcpy(h.reducer,entries[reduce].stamp.elf_hash,32);
    struct capture reduced={working.answer,0,sizeof working.answer};
    char arg[]="/work/counts.txt";
    visible_result=&working;
    int ok=execute_app(reduce,arg,tools,1,&reduced) &&
        verify_result(&f,"/work/counts.txt /work/answer.txt",tools,h.verifier,NULL);
    visible_result=&committed;
    if (!ok) return;
    if (!publish_result(&h)) { puts("result commit failed"); return; }
    printf("verified result: %s",committed.answer);
}
