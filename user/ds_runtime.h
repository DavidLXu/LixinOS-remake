/* EL0 services for independently compiled command modules. The pipeline parser
   and the resident's validated mutation broker are shared by both shells. */
unsigned ds_len(const char *s){return length(s);}
int ds_eq(const char *a,const char *b){return same(a,b);}
int ds_num(const char *s,int *v){return integer(s,v);}
int ds_emit(const char *p,unsigned n){return emit(p,n);}
void ds_put(const char *s){wordout(s);}
void ds_number(int n){number(n);}
int ds_fail(const char *message){return fail(message);}
int ds_path(const char *s,char abs[256]){return path(s,abs);}
int ds_kind(const char *s,DS *data){char abs[256];DS unused;if(!data)data=&unused;*data=(DS){0};return path(s,abs)?0:lookup(abs,data);}
int ds_input(const char *s,DS in,DS *data){if(!s||same(s,"-")){*data=in;return 0;}return readfile(s,data);}
const char *ds_cwd(void){return cwd;}
int ds_setcwd(const char *s){
    char abs[256];DS data;
    if(path(s,abs)||lookup(abs,&data)!=2)return fail("not a directory");
    int node=source_lookup(0,abs);output->cwd=node>=0?node:source_lookup(0,"/work");
    output->shell_dir[0]=0;if(node<0)copy(output->shell_dir,abs,length(abs)+1);
    return 0;
}
void ds_walk(ds_visit visit,void *ctx){visit_visible(visit,ctx);}
int ds_glob(const char *p,const char *s){return glob(p,s);}
int ds_line(DS s,unsigned *pos,DS *line){return lines(s,pos,line);}
int ds_cmp(DS a,DS b){return compare(a,b);}
int ds_match(DS s,const char *p,int icase){return match(s,p,icase);}
int ds_change(int op,const char *dest,const char *src,DS data,int append){if(!dest || (op==DS_MOVE&&!src) || (data.n&&!data.p))return fail("invalid mutation request");return change(op,dest,src,data,append);}
void ds_hash(DS data,char out[65]){
    br_sha256_context ctx;unsigned char hash[32];const char *hex="0123456789abcdef";
    br_sha256_init(&ctx);br_sha256_update(&ctx,data.p,data.n);br_sha256_out(&ctx,hash);
    for(unsigned i=0;i<32;++i){out[2*i]=hex[hash[i]>>4];out[2*i+1]=hex[hash[i]&15];}
    out[64]=0;
}
int ds_count(void){return sizeof catalog/sizeof *catalog;}
const char *ds_name(int i){return i>=0 && i<ds_count()?catalog[i].name:"";}
const char *ds_usage(int i){return i>=0 && i<ds_count()?catalog[i].usage:"";}
static int stage(int argc,char **argv,DS input){return ds_invoke(argc,argv,input);}
