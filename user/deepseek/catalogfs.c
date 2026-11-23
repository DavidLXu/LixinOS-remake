#include "ds_api.h"

typedef struct { char base[256]; int isRoot; int type; /*0 none,1 f,2 d*/ const char *glob; int baseLen; } Ctx;

static int prefix_ok(const Ctx *c, const char *abs){
    if (c->isRoot) return 1;
    unsigned bl = (unsigned)c->baseLen;
    unsigned i = 0;
    while (i < bl && abs[i] && abs[i]==c->base[i]) i++;
    if (i != bl) return 0;
    if (abs[bl]==0) return 1;
    return abs[bl]=='/';
}

static const char *basename_of(const char *abs){
    const char *b = abs;
    for (const char *p = abs; *p; p++) if (*p=='/') b = p+1;
    return b;
}

static void visit_ls(const char *abs,int kind,void *v){
    Ctx *c = (Ctx*)v;
    if (!prefix_ok(c,abs)) return;
    unsigned bl = (unsigned)c->baseLen;
    const char *suf;
    if (c->isRoot){
        if (abs[0]=='/' && abs[1]!=0) suf = abs+1;
        else return;
    } else {
        if (abs[bl]==0) return;
        suf = abs+bl+1;
        if (*suf==0) return;
    }
    int hasSlash = 0;
    for (const char *p=suf; *p; p++) if (*p=='/'){ hasSlash=1; break; }
    if (hasSlash) return;
    unsigned n = ds_len(suf);
    if (kind==2){
        char buf[300];
        unsigned k=0;
        for (unsigned i=0;i<n;i++) buf[k++]=suf[i];
        buf[k++]='/'; 
        ds_emit(buf,k);
    } else {
        ds_emit(suf,n);
    }
    ds_emit("\n",1);
}

static void visit_find(const char *abs,int kind,void *v){
    Ctx *c=(Ctx*)v;
    if (!prefix_ok(c,abs)) return;
    if (abs[0]=='/' && abs[1]==0){
        /* root: basename is root itself */
    }
    if (c->type){
        int want = (c->type==1)?1:2;
        if (kind != want) return;
    }
    if (c->glob){
        const char *b = basename_of(abs);
        if (b[0]==0) b = abs; /* root */
        if (!ds_glob(c->glob,b)) return;
    }
    ds_emit(abs,ds_len(abs));
    ds_emit("\n",1);
}

static int do_ls(int argi,int argc,char **argv){
    const char *path = ds_cwd();
    if (argi < argc) path = argv[argi];
    char abs[256];
    if (ds_path(path,abs)!=0) return ds_fail("invalid path");
    DS data;
    int k = ds_kind(abs,&data);
    if (k==0) return ds_fail("no such path");
    if (k==1){
        ds_emit(path,ds_len(path));
        ds_emit("\n",1);
        return 0;
    }
    Ctx c;
    unsigned i=0;
    while (abs[i] && i<255){ c.base[i]=abs[i]; i++; }
    c.base[i]=0;
    c.baseLen=(int)i;
    c.isRoot = (i==1 && abs[0]=='/');
    c.type=0; c.glob=0;
    ds_walk(visit_ls,&c);
    return 0;
}

static int do_find(int argi,int argc,char **argv){
    const char *path = ds_cwd();
    if (argi < argc && !(argv[argi][0]=='-' && argv[argi][1]!=0)) path = argv[argi++];
    int type=0;
    const char *glob=0;
    while (argi<argc){
        const char *a=argv[argi];
        if (ds_eq(a,"-type")){
            if (argi+1>=argc) return ds_fail("-type needs value");
            const char *v=argv[argi+1];
            if (ds_eq(v,"f")) type=1;
            else if (ds_eq(v,"d")) type=2;
            else return ds_fail("bad -type");
            argi+=2;
        } else if (ds_eq(a,"-name")){
            if (argi+1>=argc) return ds_fail("-name needs value");
            glob=argv[argi+1];
            argi+=2;
        } else return ds_fail("unknown option");
    }
    char abs[256];
    if (ds_path(path,abs)!=0) return ds_fail("invalid path");
    DS data;
    if (ds_kind(abs,&data)==0) return ds_fail("no such path");
    Ctx c;
    unsigned i=0;
    while (abs[i] && i<255){ c.base[i]=abs[i]; i++; }
    c.base[i]=0;
    c.baseLen=(int)i;
    c.isRoot = (i==1 && abs[0]=='/');
    c.type=type; c.glob=glob;
    ds_walk(visit_find,&c);
    return 0;
}

int ds_catalogfs(int argc,char **argv,DS input){
    (void)input;
    if (argc<1) return ds_fail("usage");
    const char *cmd = argv[0];
    if (ds_eq(cmd,"ls")) return do_ls(1,argc,argv);
    if (ds_eq(cmd,"find")) return do_find(1,argc,argv);
    return ds_fail("unknown command");
}
