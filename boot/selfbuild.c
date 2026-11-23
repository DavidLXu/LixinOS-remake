/* Build a complete bootable resident in a separate buffer, leaving a staged
   kernel or EL0 image intact. system.build is a list of guest source paths. */
static u8 system_image[SYSTEM_CAP];
static unsigned system_size;
static int build_system(const u8 **image,unsigned *size,int recovery) {
    int id=source_lookup(0,"/src/system.build");
    if (id<0) return 0;
    TCCState *t=tcc_new();
    if (!t) return 0;
    tcc_set_error_func(t,NULL,diagnostic);
    tcc_set_options(t,recovery?"-nostdlib -nostdinc -static -Wl,-Ttext=0x40210000":
                               "-nostdlib -nostdinc -static -Wl,-Ttext=0x42010000");
    const char *dirs[]={"/src/bootstrap/include","/src/boot","/src/kernel","/src/user",
        "/src/bootstrap","/src/net","/src/cjson","/src/lwip/include","/src/bearssl/inc",
        "/src/bearssl/src","/src/tinycc","/src/musl/internal","/src/sdk"};
    for (unsigned i=0;i<sizeof dirs/sizeof dirs[0];++i) tcc_add_include_path(t,dirs[i]);
    tcc_define_symbol(t,"GUEST_BUILD","1"); tcc_define_symbol(t,"_GNU_SOURCE","");
    tcc_define_symbol(t,"BR_USE_URANDOM","0"); tcc_define_symbol(t,"BR_INT128","0");
    tcc_define_symbol(t,"BR_UMUL128","0"); tcc_define_symbol(t,"hidden","");
    tcc_define_symbol(t,"weak_alias(a,b)","");
    int ok=tcc_set_output_type(t,TCC_OUTPUT_EXE)==0;
    if (ok) ok=add_inputs(t,"/src/system.build");
    output_name="system.elf"; output_data=system_image; output_length=&system_size;
    output_cap=SYSTEM_CAP; system_size=0;
    if (ok) ok=tcc_output_file(t,output_name)==0;
    tcc_delete(t);
    if (!ok || !valid_range(system_image,system_size,recovery?0x40200000:0x42000000,
                            recovery?0x41000000:0x44000000)) return 0;
    *image=system_image; *size=system_size; return 1;
}

static void boot_system(const u8 *image,unsigned size) {
    if (!valid_range(image,size,0x42000000,0x44000000)) return;
    const struct elf_header *h=(const void *)image;
    const struct program_header *ph=(const void *)(image+h->phoff);
    for (unsigned i=0;i<h->count;++i) {
        if (ph[i].type!=1) continue;
        memcpy((void *)ph[i].va,image+ph[i].offset,ph[i].file);
        memset((void *)(ph[i].va+ph[i].file),0,ph[i].mem-ph[i].file);
    }
    barrier(); transfer(&machine,h->entry);
}
