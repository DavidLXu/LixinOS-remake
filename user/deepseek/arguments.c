#include "ds_api.h"

#define ARENA_MAX 98305
#define MAX_ARGS 128

static char arena[ARENA_MAX];
static unsigned alen;

static int ar_reserve(unsigned n){
    if(alen+n+1>ARENA_MAX) return 0;
    return 1;
}
static char *ar_put(unsigned n){
    char *r=arena+alen;
    alen+=n;
    return r;
}

/* Parse one shell-style token starting at *pp within [p,end).
   Returns 1 if a token was produced, 0 at end, -1 on overflow. */
static int sh_token(const char **pp,const char *end,const char **tok){
    const char *p=*pp;
    char quote=0;
    unsigned start=alen;
    int any=0;
    while(p<end){
        char c=*p;
        if(quote==0){
            if(c==' '||c=='\t'||c=='\n'||c=='\r'){
                if(any) break;
                p++; continue;
            }
            if(c=='\''||c=='"'){ quote=c; p++; any=1; continue; }
            if(c=='\\'){
                p++;
                if(p>=end) break;
                if(!ar_reserve(1)) return -1;
                *ar_put(1)= *p++; any=1; continue;
            }
            if(!ar_reserve(1)) return -1;
            *ar_put(1)=c; p++; any=1; continue;
        } else if(quote=='"'){
            if(c=='"'){ quote=0; p++; continue; }
            if(c=='\\' && p+1<end){
                char d=p[1];
                if(d=='"'||d=='\\'||d=='$'||d=='`'){
                    if(!ar_reserve(1)) return -1;
                    *ar_put(1)=d; p+=2; any=1; continue;
                }
            }
            if(!ar_reserve(1)) return -1;
            *ar_put(1)=c; p++; any=1; continue;
        } else {
            if(c=='\''){ quote=0; p++; continue; }
            if(!ar_reserve(1)) return -1;
            *ar_put(1)=c; p++; any=1; continue;
        }
    }
    *pp=p;
    if(!any) return 0;
    if(!ar_reserve(1)) return -1;
    *ar_put(1)=0;
    *tok=arena+start;
    return 1;
}

static int run_batch(char **base,int nbase,char **tokv,int ntok){
    if(ntok==0) return 0;
    int total=nbase+ntok;
    if(total>MAX_ARGS) return -1;
    char *argv[MAX_ARGS+1];
    int i;
    for(i=0;i<nbase;i++) argv[i]=base[i];
    for(i=0;i<ntok;i++) argv[nbase+i]=tokv[i];
    argv[total]=0;
    DS empty={0,0};
    return ds_invoke(total,argv,empty);
}

int ds_arguments(int argc,char **argv,DS input){
    int i;
    long per=64;
    char delim=0;
    for(i=1;i<argc;i++){
        if(argv[i][0]=='-'&&argv[i][1]){
            const char *a=argv[i];
            if(ds_eq(a,"-n")){
                int v;
                if(i+1>=argc) return ds_fail("xargs: -n requires a value");
                if(!ds_num(argv[i+1],&v)||v<1) return ds_fail("xargs: invalid -n value");
                per=v; i++;
            } else if(ds_eq(a,"-d")){
                if(i+1>=argc) return ds_fail("xargs: -d requires a value");
                const char *d=argv[i+1];
                if(ds_eq(d,"\\n")) delim='\n';
                else if(d[0]&&!d[1]) delim=d[0];
                else return ds_fail("xargs: -d takes one byte or \\n");
                i++;
            } else if(ds_eq(a,"--")){ i++; break; }
            else return ds_fail("xargs: unknown option");
        } else break;
    }
    if(i>=argc) return ds_fail("xargs: missing COMMAND");
    int cstart=i;
    const char *target=argv[cstart];
    if(ds_eq(target,"xargs")) return ds_fail("xargs: refusing to run xargs");
    if(ds_eq(target,"cd")) return ds_fail("xargs: refusing to run cd");
    int nbase=argc-cstart;
    if(nbase>MAX_ARGS) return ds_fail("xargs: too many fixed arguments");

    DS data;
    if(ds_input("-",input,&data)!=0) return ds_fail("xargs: cannot read input");

    unsigned pos=0;
    int batch_rc=0;
    char *tokv[MAX_ARGS];
    int ntok=0;
    alen=0;

    if(delim){
        unsigned seg=0;
        unsigned s;
        unsigned n=data.n;
        for(s=0;s<=n;s++){
            int at_end=(s==n);
            int is_delim=at_end?0:(data.p[s]==delim);
            if(is_delim || at_end){
                /* final byte delimiter: do not synthesize trailing empty */
                if(at_end && n>0 && data.p[n-1]==delim) break;
                unsigned len=(s-seg);
                const char *t;
                if(len>0){
                    if(!ar_reserve(len)) return ds_fail("xargs: input too large");
                    char *tp=ar_put(len);
                    unsigned k;
                    for(k=0;k<len;k++) tp[k]=data.p[seg+k];
                    t=tp;
                } else {
                    t="";
                }
                if(!ar_reserve(1)) return ds_fail("xargs: input too large");
                *ar_put(1)=0;
                tokv[ntok++]= (char*)t;
                seg=s+1;
                if((long)ntok>=per || ntok>=MAX_ARGS){
                    int r=run_batch(argv+cstart,nbase,tokv,ntok);
                    if(r<0) return ds_fail("xargs: too many arguments");
                    if(r==1) batch_rc=1;
                    ntok=0; alen=0;
                }
                if(at_end) break;
            }
        }
    } else {
        while(pos<data.n){
            const char *p=data.p+pos;
            const char *end=data.p+data.n;
            const char *tok=0;
            int r=sh_token(&p,end,&tok);
            if(r<0) return ds_fail("xargs: token too large");
            pos=(unsigned)(p-data.p);
            if(r==0) break;
            tokv[ntok++]= (char*)tok;
            if((long)ntok>=per || ntok>=MAX_ARGS){
                int rc=run_batch(argv+cstart,nbase,tokv,ntok);
                if(rc<0) return ds_fail("xargs: too many arguments");
                if(rc==1) batch_rc=1;
                ntok=0; alen=0;
            }
        }
    }
    if(ntok>0){
        int rc=run_batch(argv+cstart,nbase,tokv,ntok);
        if(rc<0) return ds_fail("xargs: too many arguments");
        if(rc==1) batch_rc=1;
    }
    return batch_rc?1:0;
}
