#include "ds_api.h"

static int parse_sort(int argc,char **argv,int *rev,int *uniq,int *fileidx){
  int i;
  *rev=0;*uniq=0;*fileidx=-1;
  for(i=1;i<argc;i++){
    const char *a=argv[i];
    if(a[0]=='-'&&a[1]!='\0'&&a[1]!='-'){
      const char *p=a+1;
      while(*p){
        if(*p=='r')*rev=1;
        else if(*p=='u')*uniq=1;
        else return ds_fail("sort: invalid option");
        p++;
      }
    } else {
      if(*fileidx!=-1)return ds_fail("sort: multiple files");
      *fileidx=i;
    }
  }
  return 0;
}

static int parse_uniq(int argc,char **argv,int *count,int *fileidx){
  int i;
  *count=0;*fileidx=-1;
  for(i=1;i<argc;i++){
    const char *a=argv[i];
    if(a[0]=='-'&&a[1]!='\0'&&a[1]!='-'){
      const char *p=a+1;
      while(*p){
        if(*p=='c')*count=1;
        else return ds_fail("uniq: invalid option");
        p++;
      }
    } else {
      if(*fileidx!=-1)return ds_fail("uniq: multiple files");
      *fileidx=i;
    }
  }
  return 0;
}

static void sift(DS *arr,int start,int end,int rev){
  int root=start;
  while(root*2+1<=end){
    int child=root*2+1;
    int swap=root;
    int c=ds_cmp(arr[swap],arr[child]);
    if((!rev&&c<0)||(rev&&c>0))swap=child;
    if(child+1<=end){
      c=ds_cmp(arr[swap],arr[child+1]);
      if((!rev&&c<0)||(rev&&c>0))swap=child+1;
    }
    if(swap!=root){
      DS tmp=arr[root];arr[root]=arr[swap];arr[swap]=tmp;root=swap;
    } else return;
  }
}

static void heapsort(DS *arr,int n,int rev){
  int start=(n-2)/2;
  int end=n-1;
  for(;;){
    sift(arr,start,end,rev);
    if(start==0)break;
    start--;
  }
  while(end>0){
    DS tmp=arr[0];arr[0]=arr[end];arr[end]=tmp;
    end--;
    sift(arr,0,end,rev);
  }
}

static int emit_line(DS line){
  if(ds_emit(line.p,line.n))return 1;
  if(ds_emit("\n",1))return 1;
  return 0;
}

static int do_sort(int argc,char **argv,DS input){
  int rev,uniq,fileidx,rc,i,n;
  DS data;DS line;unsigned pos=0;
  static DS lines[8192];
  rc=parse_sort(argc,argv,&rev,&uniq,&fileidx);
  if(rc)return rc;
  rc=ds_input(fileidx>=0?argv[fileidx]:NULL,input,&data);
  if(rc)return ds_fail("sort: cannot read input");
  n=0;
  while(ds_line(data,&pos,&line)){
    if(n>=8192)return ds_fail("sort: too many lines");
    lines[n++]=line;
  }
  if(n==0)return 0;
  heapsort(lines,n,rev);
  if(uniq){
    for(i=0;i<n;i++){
      if(i==0||ds_cmp(lines[i-1],lines[i])!=0)
        if(emit_line(lines[i]))return 1;
    }
  } else {
    for(i=0;i<n;i++)if(emit_line(lines[i]))return 1;
  }
  return 0;
}

static int do_uniq(int argc,char **argv,DS input){
  int count,fileidx,rc;
  DS data,prev,line;unsigned pos=0;
  int have=0;int run=0;
  rc=parse_uniq(argc,argv,&count,&fileidx);
  if(rc)return rc;
  rc=ds_input(fileidx>=0?argv[fileidx]:NULL,input,&data);
  if(rc)return ds_fail("uniq: cannot read input");
  while(ds_line(data,&pos,&line)){
    if(have&&ds_cmp(prev,line)==0){run++;}
    else{
      if(have){
        if(count){ds_number(run);if(ds_emit(" ",1))return 1;}
        if(emit_line(prev))return 1;
      }
      prev=line;have=1;run=1;
    }
  }
  if(have){
    if(count){ds_number(run);if(ds_emit(" ",1))return 1;}
    if(emit_line(prev))return 1;
  }
  return 0;
}

int ds_sorting(int argc,char **argv,DS input){
  const char *cmd;
  if(argc<1||argv==NULL||argv[0]==NULL)return ds_fail("sorting: missing command");
  cmd=argv[0];
  if(ds_eq(cmd,"sort"))return do_sort(argc,argv,input);
  if(ds_eq(cmd,"uniq"))return do_uniq(argc,argv,input);
  return ds_fail("sorting: unknown command");
}
