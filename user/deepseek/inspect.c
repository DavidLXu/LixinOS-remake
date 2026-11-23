#include "ds_api.h"

static int resolve(const char *name,char buf[256]){
  if(ds_path(name,buf)!=0) return ds_fail("invalid path");
  return 0;
}

static int do_stat(int argc,char **argv){
  char path[256];
  DS data;
  int k;
  if(argc<2) return ds_fail("stat: missing operand");
  if(argc>2) return ds_fail("stat: too many operands");
  if(resolve(argv[1],path)!=0) return 1;
  k=ds_kind(path,&data);
  if(k==0) return ds_fail("stat: no such file or directory");
  if(k==2){ ds_put("directory "); ds_put(path); ds_put("\n"); return 0;}
  ds_put("file ");
  ds_number((int)data.n);
  ds_put(" bytes ");
  ds_put(path);
  ds_put("\n");
  return 0;
}

static int read_file(const char *name,DS *out){
  char path[256];
  int k;
  if(resolve(name,path)!=0) return ds_fail("invalid path");
  k=ds_kind(path,out);
  if(k==0) return ds_fail("cmp: no such file or directory");
  if(k==2) return ds_fail("cmp: is a directory");
  return 0;
}

static int do_cmp(int argc,char **argv){
  DS a,b;
  if(argc<2) return ds_fail("cmp: missing operand");
  if(argc<3) return ds_fail("cmp: missing second operand");
  if(argc>3) return ds_fail("cmp: too many operands");
  if(read_file(argv[1],&a)!=0) return 1;
  if(read_file(argv[2],&b)!=0) return 1;
  if(ds_cmp(a,b)==0) return 0;
  return 1;
}

static int exists_mode(const char *path,int mode){
  DS data;
  int k=ds_kind(path,&data);
  if(mode==0) return k!=0;
  if(mode==1) return k==1;
  return k==2;
}

static int is_unary(const char *s){
  if(ds_eq(s,"-e")||ds_eq(s,"-f")||ds_eq(s,"-d")||
     ds_eq(s,"-n")||ds_eq(s,"-z")) return 1;
  return 0;
}

static int do_test(int argc,char **argv){
  int neg=0;
  int i=1;
  int r;
  if(argc<2) return ds_fail("test: missing operand");
  if(ds_eq(argv[i],"!")){
    neg=1;
    i++;
    if(i>=argc) return ds_fail("test: invalid expression");
  }
  /* Binary string equality handled directly, before unary tests. */
  if(argc-i>=3){
    if(argc-i>3) return ds_fail("test: invalid expression");
    if(ds_eq(argv[i+1],"=")){
      if(i+2>=argc) return ds_fail("test: invalid expression");
      r=ds_eq(argv[i],argv[i+2])?0:1;
      if(neg) r=r?0:1;
      return r;
    }
    return ds_fail("test: invalid expression");
  }
  if(is_unary(argv[i])){
    const char *op=argv[i];
    const char *operand;
    if(i+1>=argc) return ds_fail("test: invalid expression");
    operand=argv[i+1];
    if(i+2<argc) return ds_fail("test: invalid expression");
    if(ds_eq(op,"-n")) r=ds_len(operand)>0;
    else if(ds_eq(op,"-z")) r=ds_len(operand)==0;
    else {
      char path[256];
      if(resolve(operand,path)!=0) return 1;
      if(ds_eq(op,"-e")) r=exists_mode(path,0);
      else if(ds_eq(op,"-f")) r=exists_mode(path,1);
      else r=exists_mode(path,2);
    }
    if(neg) r=!r;
    return r?0:1;
  }
  if(i+1<argc) return ds_fail("test: invalid expression");
  r=ds_len(argv[i])>0;
  if(neg) r=!r;
  return r?0:1;
}

int ds_inspect(int argc,char **argv,DS input){
  (void)input;
  if(argc<1) return ds_fail("no command");
  if(ds_eq(argv[0],"stat")) return do_stat(argc,argv);
  if(ds_eq(argv[0],"cmp")) return do_cmp(argc,argv);
  if(ds_eq(argv[0],"test")) return do_test(argc,argv);
  return ds_fail("unknown command");
}
