#include "ds_api.h"

static unsigned sed_match(const char *s,unsigned avail,const char *old,unsigned oldlen){
  unsigned k=0;
  if(oldlen>avail)return 0;
  while(k<oldlen){if(s[k]!=old[k])return 0;k++;}
  return 1;
}

/* Walk bytes of data, applying replacement; each position either matches once-per-line
   (unless global) or emits one byte. LF resets the per-line replaced latch. */
int ds_edit(int argc,char **argv,DS input){
  int i=1,inplace=0;
  const char *expr=0,*file=0;
  while(i<argc){
    const char *a=argv[i];
    if(a[0]=='-'&&a[1]=='i'&&a[2]=='\0'){inplace=1;i++;continue;}
    if(!expr){expr=a;i++;continue;}
    if(!file){file=a;i++;continue;}
    return ds_fail("too many arguments");
  }
  if(!expr)return ds_fail("missing expression");
  if(expr[0]!='s')return ds_fail("expression must start with s");
  char d=expr[1];
  if(d=='\0'||d=='\n')return ds_fail("bad delimiter");
  const char *p=expr+2;
  const char *old=p;
  while(*p&&*p!=d)p++;
  if(*p!=d)return ds_fail("missing closing delimiter for OLD");
  unsigned oldlen=(unsigned)(p-old);
  if(oldlen==0)return ds_fail("OLD must not be empty");
  p++;
  const char *nw=p;
  while(*p&&*p!=d)p++;
  if(*p!=d)return ds_fail("missing closing delimiter for NEW");
  unsigned newlen=(unsigned)(p-nw);
  p++;
  int global=0;
  if(*p=='g'){global=1;p++;}
  if(*p!='\0')return ds_fail("invalid suffix");
  if(inplace&&!file)return ds_fail("-i requires a file");

  DS data;
  if(file){
    DS none;none.p=0;none.n=0;
    if(ds_input(file,none,&data))return ds_fail("cannot read file");
  }else{
    if(ds_input("-",input,&data))return ds_fail("cannot read input");
  }

  if(!inplace){
    unsigned pos=0;int replaced=0;
    while(pos<data.n){
      char c=data.p[pos];
      if(!replaced||global){
        if(sed_match(data.p+pos,data.n-pos,old,oldlen)){
          if(newlen&&ds_emit(nw,newlen))return ds_fail("write failed");
          pos+=oldlen;
          replaced=1;
          continue;
        }
      }
      if(ds_emit(&c,1))return ds_fail("write failed");
      if(c=='\n')replaced=0;
      pos++;
    }
    return 0;
  }

  /* in-place: accumulate into a heap-managed static chunk list is not available;
     we rebuild in a bounded static buffer with overflow guard. */
  static char buf[65536];
  unsigned blen=0;int ovf=0;
  unsigned pos=0;int replaced=0;
  while(pos<data.n){
    char c=data.p[pos];
    if(!replaced||global){
      if(sed_match(data.p+pos,data.n-pos,old,oldlen)){
        unsigned j=0;
        while(j<newlen){if(blen<sizeof(buf))buf[blen++]=nw[j];else ovf=1;j++;}
        pos+=oldlen;
        replaced=1;
        continue;
      }
    }
    if(blen<sizeof(buf))buf[blen++]=c;else ovf=1;
    if(c=='\n')replaced=0;
    pos++;
  }
  if(ovf)return ds_fail("output too large for in-place edit");
  DS out;out.p=buf;out.n=blen;
  if(ds_change(DS_WRITE,file,0,out,0))return ds_fail("write failed");
  return 0;
}
