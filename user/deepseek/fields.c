#include "ds_api.h"

static int aw_isspace(char c){return c==' '||c=='\t'||c=='\r'||c=='\v'||c=='\f';}

static void aw_field(DS line,int idx,DS *out){
    unsigned i=0;int cur=0;
    out->p=0;out->n=0;
    while(i<line.n){
        while(i<line.n&&aw_isspace(line.p[i]))i++;
        if(i>=line.n)break;
        unsigned s=i;
        while(i<line.n&&!aw_isspace(line.p[i]))i++;
        cur++;
        if(cur==idx){out->p=line.p+s;out->n=i-s;return;}
    }
}

static int aw_count(DS line){
    unsigned i=0;int c=0;
    while(i<line.n){
        while(i<line.n&&aw_isspace(line.p[i]))i++;
        if(i>=line.n)break;
        c++;
        while(i<line.n&&!aw_isspace(line.p[i]))i++;
    }
    return c;
}

static int parse_int_local(const char *s,unsigned n,int *v){
    unsigned i=0;int neg=0;long long r=0;
    if(i<n&&(s[i]=='-'||s[i]=='+')){neg=(s[i]=='-');i++;}
    if(i>=n)return 0;
    for(;i<n;i++){if(s[i]<'0'||s[i]>'9')return 0;r=r*10+(s[i]-'0');if(r>2147483647LL)return 0;}
    *v=neg?-(int)r:(int)r;return 1;
}

enum {AW_RECORD, AW_NFIELD, AW_COUNT, AW_NR};

static int run_awk(int argc,char **argv,DS input){
    const char *prog=argv[1];
    unsigned pl=ds_len(prog);
    int kind=-1;int idx=0;
    unsigned i=0;
    while(i<pl&&(prog[i]==' '||prog[i]=='\t'))i++;
    if(i>=pl||prog[i]!='{')return ds_fail("awk: program must be {print ...}");
    i++;
    while(i<pl&&(prog[i]==' '||prog[i]=='\t'))i++;
    const char *kw="print";unsigned k=0;
    while(k<5&&i<pl&&prog[i]==kw[k]){i++;k++;}
    if(k<5)return ds_fail("awk: expected print");
    while(i<pl&&(prog[i]==' '||prog[i]=='\t'))i++;

    if(i+1<pl&&prog[i]=='N'&&prog[i+1]=='F'){kind=AW_COUNT;i+=2;}
    else if(i+1<pl&&prog[i]=='N'&&prog[i+1]=='R'){kind=AW_NR;i+=2;}
    else if(i<pl&&prog[i]=='$'){
        i++;
        unsigned s=i;
        while(i<pl&&prog[i]>='0'&&prog[i]<='9')i++;
        if(s==i)return ds_fail("awk: missing field number");
        int v;
        if(!parse_int_local(prog+s,i-s,&v)||v<0)return ds_fail("awk: bad field number");
        if(v==0){kind=AW_RECORD;}
        else{kind=AW_NFIELD;idx=v;}
    } else return ds_fail("awk: unsupported expression");

    while(i<pl&&(prog[i]==' '||prog[i]=='\t'))i++;
    if(i>=pl||prog[i]!='}')return ds_fail("awk: expected }");
    i++;
    while(i<pl&&(prog[i]==' '||prog[i]=='\t'))i++;
    if(i<pl)return ds_fail("awk: trailing characters");

    DS data;
    const char *file=NULL;
    if(argc>2)file=argv[2];
    if(argc>3)return ds_fail("awk: too many operands");
    if(ds_input(file,input,&data))return ds_fail("awk: cannot read input");

    unsigned pos=0;DS line;int nr=0;
    while(ds_line(data,&pos,&line)){
        nr++;
        if(kind==AW_NR){ds_number(nr);ds_emit("\n",1);continue;}
        if(kind==AW_COUNT){ds_number(aw_count(line));ds_emit("\n",1);continue;}
        if(kind==AW_RECORD){if(line.n&&ds_emit(line.p,line.n))return ds_fail("awk: write error");ds_emit("\n",1);continue;}
        DS f;aw_field(line,idx,&f);
        if(f.n&&ds_emit(f.p,f.n))return ds_fail("awk: write error");
        if(ds_emit("\n",1))return ds_fail("awk: write error");
    }
    return 0;
}

static int run_cut(int argc,char **argv,DS input){
    char delim='\t';int field=-1;const char *file=NULL;
    int a=1;int gotf=0;
    while(a<argc){
        const char *t=argv[a];
        if(t[0]=='-'&&t[1]=='d'){
            if(t[2]){if(t[3])return ds_fail("cut: -d needs single char");delim=t[2];}
            else{a++;if(a>=argc)return ds_fail("cut: -d needs arg");if(!argv[a][0]||argv[a][1])return ds_fail("cut: -d needs single char");delim=argv[a][0];}
            a++;
        } else if(t[0]=='-'&&t[1]=='f'){
            const char *num;
            if(t[2])num=t+2;
            else{a++;if(a>=argc)return ds_fail("cut: -f needs arg");num=argv[a];}
            unsigned nl=ds_len(num);
            if(!parse_int_local(num,nl,&field)||field<1)return ds_fail("cut: field must be positive integer");
            gotf=1;a++;
        } else if(t[0]=='-'&&t[1]){
            return ds_fail("cut: unknown option");
        } else {
            if(file)return ds_fail("cut: too many operands");
            file=t;a++;
        }
    }
    if(!gotf)return ds_fail("cut: -f required");
    DS data;
    if(ds_input(file,input,&data))return ds_fail("cut: cannot read input");
    unsigned pos=0;DS line;
    while(ds_line(data,&pos,&line)){
        int has=0;
        unsigned i;
        for(i=0;i<line.n;i++){if(line.p[i]==delim){has=1;break;}}
        if(!has){
            if(line.n&&ds_emit(line.p,line.n))return ds_fail("cut: write error");
            if(ds_emit("\n",1))return ds_fail("cut: write error");
            continue;
        }
        unsigned s=0;int cur=1;int done=0;
        for(i=0;i<=line.n;i++){
            if(i==line.n||line.p[i]==delim){
                if(cur==field){
                    if(i>s&&ds_emit(line.p+s,i-s))return ds_fail("cut: write error");
                    done=1;break;
                }
                cur++;s=i+1;
            }
        }
        (void)done;
        if(ds_emit("\n",1))return ds_fail("cut: write error");
    }
    return 0;
}

int ds_fields(int argc,char **argv,DS input){
    if(argc<1)return ds_fail("fields: missing command");
    if(ds_eq(argv[0],"cut"))return run_cut(argc,argv,input);
    if(ds_eq(argv[0],"awk")){
        if(argc<2)return ds_fail("awk: missing program");
        return run_awk(argc,argv,input);
    }
    return ds_fail("fields: unknown command");
}
