#include "ds_api.h"

#define MAXSET 512

typedef struct {
  unsigned char lo;
  unsigned char hi;
} Range;

typedef struct {
  Range r[MAXSET];
  int n;
} Set;

static int is_dash(int c){ return c=='-'; }

static int parse_set(const char *s, unsigned len, Set *out){
  unsigned i=0;
  out->n=0;
  if(len==0) return -1;
  while(i<len){
    unsigned char c=(unsigned char)s[i];
    if(i+1<len && is_dash((unsigned char)s[i+1])){
      if(i+2>=len) return -2; /* dangling dash */
      unsigned char e=(unsigned char)s[i+2];
      if(e<c) return -3; /* reversed */
      if(out->n>=MAXSET) return -4;
      out->r[out->n].lo=c;
      out->r[out->n].hi=e;
      out->n++;
      i+=3;
    } else {
      /* literal, but check for reversed range where '-' came before? handled above */
      if(out->n>=MAXSET) return -4;
      out->r[out->n].lo=c;
      out->r[out->n].hi=c;
      out->n++;
      i++;
    }
  }
  if(out->n==0) return -1;
  return 0;
}

/* Expand set into an ordered array; return count, -1 on overflow */
static int expand_set(const Set *s, unsigned char *buf, int max){
  int k=0;
  int i,j;
  for(i=0;i<s->n;i++){
    for(j=(int)s->r[i].lo;j<=(int)s->r[i].hi;j++){
      if(k>=max) return -1;
      buf[k++]=(unsigned char)j;
    }
  }
  return k;
}

/* find index of byte in expanded set; -1 if absent */
static int find_in(const unsigned char *buf,int n,unsigned char c){
  int i;
  for(i=0;i<n;i++) if(buf[i]==c) return i;
  return -1;
}

static int set_in(const Set *s,int c){
  int i;
  for(i=0;i<s->n;i++){
    if(c>=(int)s->r[i].lo && c<=(int)s->r[i].hi) return 1;
  }
  return 0;
}

static int do_delete(DS input, const Set *del){
  unsigned pos=0;
  DS line;
  while(pos<input.n){
    unsigned start=pos;
    unsigned char ch=(unsigned char)input.p[pos];
    (void)start;
    /* process byte by byte while respecting lines; simpler: walk bytes */
    if(!set_in(del,(int)ch)){
      const char *q=&input.p[pos];
      unsigned k=pos;
      while(k<input.n && !set_in(del,(unsigned char)input.p[k])) k++;
      if(ds_emit(q,k-pos)!=0) return 1;
      pos=k;
    } else {
      pos++;
    }
  }
  (void)line;
  return 0;
}

static int do_translate(DS input, const Set *from, const Set *to){
  unsigned char fb[1024];
  unsigned char tb[1024];
  int fn=expand_set(from,fb,1024);
  int tn=expand_set(to,tb,1024);
  unsigned pos=0;
  if(fn<=0) return 1;
  if(tn<=0) return 1;
  while(pos<input.n){
    unsigned char ch=(unsigned char)input.p[pos];
    int idx=find_in(fb,fn,ch);
    unsigned char oc;
    if(idx>=0){
      if(idx<tn) oc=tb[idx];
      else oc=tb[tn-1];
    } else oc=ch;
    /* emit contiguous chunk not mapped? simpler emit one byte but batch runs */
    unsigned k=pos;
    while(k<input.n){
      unsigned char cc=(unsigned char)input.p[k];
      int ii=find_in(fb,fn,cc);
      if(ii>=0){
        unsigned char out;
        if(ii<tn) out=tb[ii]; else out=tb[tn-1];
        if(k==pos){
          /* need single byte; fallthrough to emit one */
          break;
        }
        if(out!=cc) break;
      }
      k++;
    }
    if(k>pos){
      if(ds_emit(&input.p[pos],k-pos)!=0) return 1;
      pos=k;
    } else {
      unsigned char out=oc;
      if(ds_emit(&out,1)!=0) return 1;
      pos++;
    }
  }
  return 0;
}

int ds_translate(int argc,char **argv,DS input){
  int delete_mode=0;
  int a=1;
  DS data;
  if(argc<2) return ds_fail("usage: tr SET SET or tr -d SET");
  if(argc>=2 && argv[1] && ds_len(argv[1])==2 && argv[1][0]=='-' && argv[1][1]=='d'){
    delete_mode=1;
    a=2;
  }
  if(delete_mode){
    if(argc!=3) return ds_fail("tr -d requires exactly one SET");
    Set del;
    int p=parse_set(argv[2],ds_len(argv[2]),&del);
    if(p==-1) return ds_fail("empty set");
    if(p==-2) return ds_fail("dangling dash in set");
    if(p==-3) return ds_fail("reversed range in set");
    if(p==-4) return ds_fail("set too large");
    if(ds_input(NULL,input,&data)!=0) return ds_fail("cannot read input");
    return do_delete(data,&del);
  } else {
    Set from,to;
    int p;
    if(argc!=3) return ds_fail("tr requires two SET arguments");
    p=parse_set(argv[1],ds_len(argv[1]),&from);
    if(p==-1) return ds_fail("empty set");
    if(p==-2) return ds_fail("dangling dash in set");
    if(p==-3) return ds_fail("reversed range in set");
    if(p==-4) return ds_fail("set too large");
    p=parse_set(argv[2],ds_len(argv[2]),&to);
    if(p==-1) return ds_fail("empty set");
    if(p==-2) return ds_fail("dangling dash in set");
    if(p==-3) return ds_fail("reversed range in set");
    if(p==-4) return ds_fail("set too large");
    if(ds_input(NULL,input,&data)!=0) return ds_fail("cannot read input");
    return do_translate(data,&from,&to);
  }
}
