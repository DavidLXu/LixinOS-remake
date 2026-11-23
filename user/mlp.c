#include "app.h"

#define S 4096
#define MAXN 64

static int abs_i(int v){return v<0?-v:v;}

static int tokenizer(const char *buf,int len,int *pos,long long *out){
    int i=*pos;
    while(i<len && (buf[i]==' '||buf[i]=='\t'||buf[i]=='\n'||buf[i]=='\r')) i++;
    if(i>=len){*pos=i;return 0;}
    int neg=0;
    if(buf[i]=='-'||buf[i]=='+'){neg=buf[i]=='-';i++;}
    if(i>=len) return -1;
    if(buf[i]<'0'||buf[i]>'9') return -1;
    long long v=0;
    int nd=0;
    while(i<len && buf[i]>='0'&&buf[i]<='9'){
        v=v*10+(buf[i]-'0');
        if(v>100000000LL) return -1;
        i++;nd++;
    }
    if(nd==0) return -1;
    if(i<len){
        char c=buf[i];
        if(!(c==' '||c=='\t'||c=='\n'||c=='\r')) return -1;
    }
    *out=neg?-v:v;
    *pos=i;
    return 1;
}

static int parse_file(const char *path,int *xs,int *ys,int *lab,int *n){
    int fd=file_open(path);
    if(fd<0) return -1;
    static char buf[8192];
    int total=0;
    while(total<8000){
        int r=file_read(fd,buf+total,8000-total);
        if(r<0){file_close(fd);return -1;}
        if(r==0) break;
        total+=r;
    }
    char extra;
    if(file_read(fd,&extra,1)>0){file_close(fd);return -1;}
    file_close(fd);
    int pos=0,cnt=0;
    while(1){
        long long t;
        int r=tokenizer(buf,total,&pos,&t);
        if(r==0) break;
        if(r<0) return -1;
        if(cnt>=MAXN*3) return -1;
        int idx=cnt;
        if(idx%3==0) xs[idx/3]=(int)t;
        else if(idx%3==1) ys[idx/3]=(int)t;
        else lab[idx/3]=(int)t;
        cnt++;
    }
    if(cnt==0||cnt%3!=0) return -1;
    *n=cnt/3;
    if(*n>MAXN) return -1;
    for(int i=0;i<*n;i++){
        if(xs[i]<-16||xs[i]>16||ys[i]<-16||ys[i]>16) return -1;
        if(lab[i]!=0&&lab[i]!=1) return -1;
    }
    return 0;
}

static int hw[4][2],hb[4],ow[4],ob;

static void forward(int x,int y,int h[4],int *out){
    int sx=(x*S)/10;
    int sy=(y*S)/10;
    for(int k=0;k<4;k++){
        long long a=(long long)hw[k][0]*sx+(long long)hw[k][1]*sy;
        a=a/S+hb[k];
        h[k]=a>0?(int)a:0;
    }
    long long o=ob;
    for(int k=0;k<4;k++) o+=(long long)ow[k]*h[k]/S;
    *out=(int)o;
}

static int argc_ok(int argc,char **argv){return argc>=4;}

int main(int argc,char **argv){
    if(!argc_ok(argc,argv)) return 1;
    int xs[MAXN],ys[MAXN],lab[MAXN],n=0;
    if(parse_file(argv[1],xs,ys,lab,&n)!=0) return 1;
    int epochs=0;
    {
        const char *p=argv[2];
        if(!*p) return 1;
        while(*p){
            if(*p<'0'||*p>'9') return 1;
            epochs=epochs*10+(*p-'0');
            if(epochs>100000) return 1;
            p++;
        }
    }
    if(epochs>300) return 1;
    if(argv[3][0]=='\0') return 1;
    int isloss=0,ispred=0;
    {
        const char *m=argv[3];
        const char *a="loss";
        const char *b="predict";
        int i=0;while(m[i]&&a[i]&&m[i]==a[i])i++;
        if(!m[i]&&!a[i]) isloss=1;
        i=0;while(m[i]&&b[i]&&m[i]==b[i])i++;
        if(!m[i]&&!b[i]) ispred=1;
    }
    if(!isloss&&!ispred) return 1;
    if(isloss){
        if(argc!=4) return 1;
    } else {
        int cnt=argc-4;
        if(cnt<2||cnt%2!=0) return 1;
        int pn=cnt/2;
        if(pn>32) return 1;
        int px[32],py[32];
        for(int i=0;i<pn;i++){
            const char *p=argv[4+i*2];
            int neg=0;
            if(*p=='-'){neg=1;p++;}
            if(!*p) return 1;
            long long v=0;
            while(*p){if(*p<'0'||*p>'9')return 1;v=v*10+(*p-'0');if(v>1000)return 1;p++;}
            px[i]=neg?-(int)v:(int)v;
            p=argv[5+i*2];
            neg=0;
            if(*p=='-'){neg=1;p++;}
            if(!*p) return 1;
            v=0;
            while(*p){if(*p<'0'||*p>'9')return 1;v=v*10+(*p-'0');if(v>1000)return 1;p++;}
            py[i]=neg?-(int)v:(int)v;
        }
        hw[0][0]=S/2;hw[0][1]=S/2;
        hw[1][0]=S/2;hw[1][1]=-S/2;
        hw[2][0]=-S/2;hw[2][1]=S/2;
        hw[3][0]=-S/2;hw[3][1]=-S/2;
        for(int k=0;k<4;k++){hb[k]=S/8;ow[k]=0;}
        ob=S/2;
        for(int e=0;e<epochs;e++){
            long long gdw[4][2]={0},gdb[4]={0},gow[4]={0},gob=0;
            for(int i=0;i<n;i++){
                int h[4],o;
                forward(xs[i],ys[i],h,&o);
                long long t=(long long)lab[i]*S;
                long long err=o-t;
                gob+=2*err;
                for(int k=0;k<4;k++) gow[k]+=2*err*h[k]/S;
                for(int k=0;k<4;k++){
                    if(h[k]>0){
                        int sx=(xs[i]*S)/10,sy=(ys[i]*S)/10;
                        long long d=2*err*ow[k]/S;
                        gdw[k][0]+=d*sx/S;
                        gdw[k][1]+=d*sy/S;
                        gdb[k]+=d;
                    }
                }
            }
            ob-=(int)(gob/n/8);
            for(int k=0;k<4;k++){
                ow[k]-=(int)(gow[k]/n/8);
                hw[k][0]-=(int)(gdw[k][0]/n/8);
                hw[k][1]-=(int)(gdw[k][1]/n/8);
                hb[k]-=(int)(gdb[k]/n/8);
            }
        }
        for(int i=0;i<pn;i++){
            int h[4],o;
            forward(px[i],py[i],h,&o);
            if(i) putchar(' ');
            putchar(o>=S/2?'1':'0');
        }
        putchar('\n');
        return 0;
    }
    hw[0][0]=S/2;hw[0][1]=S/2;
    hw[1][0]=S/2;hw[1][1]=-S/2;
    hw[2][0]=-S/2;hw[2][1]=S/2;
    hw[3][0]=-S/2;hw[3][1]=-S/2;
    for(int k=0;k<4;k++){hb[k]=S/8;ow[k]=0;}
    ob=S/2;
    for(int e=0;e<epochs;e++){
        long long gdw[4][2]={0},gdb[4]={0},gow[4]={0},gob=0;
        for(int i=0;i<n;i++){
            int h[4],o;
            forward(xs[i],ys[i],h,&o);
            long long t=(long long)lab[i]*S;
            long long err=o-t;
            gob+=2*err;
            for(int k=0;k<4;k++) gow[k]+=2*err*h[k]/S;
            for(int k=0;k<4;k++){
                if(h[k]>0){
                    int sx=(xs[i]*S)/10,sy=(ys[i]*S)/10;
                    long long d=2*err*ow[k]/S;
                    gdw[k][0]+=d*sx/S;
                    gdw[k][1]+=d*sy/S;
                    gdb[k]+=d;
                }
            }
        }
        ob-=(int)(gob/n/8);
        for(int k=0;k<4;k++){
            ow[k]-=(int)(gow[k]/n/8);
            hw[k][0]-=(int)(gdw[k][0]/n/8);
            hw[k][1]-=(int)(gdw[k][1]/n/8);
            hb[k]-=(int)(gdb[k]/n/8);
        }
    }
    long long tot=0;
    for(int i=0;i<n;i++){
        int h[4],o;
        forward(xs[i],ys[i],h,&o);
        long long t=(long long)lab[i]*S;
        long long d=o-t;
        tot+=d*d;
    }
    long long mse=tot*1000000LL/((long long)n*S*S);
    {
        char b[32];int len=0;
        if(mse==0){b[len++]='0';}
        else{while(mse>0){b[len++]='0'+(mse%10);mse/=10;}}
        for(int i=len-1;i>=0;i--) putchar(b[i]);
    }
    putchar('\n');
    return 0;
}
