/* Drop records with the requested first field, retaining the other bytes. */
#include "app.h"
int main(int argc,char **argv) {
    if (argc!=3) return 1;
    int fd=file_open(argv[1]); if (fd<0) return 1;
    char line[512],byte; unsigned used=0; int n;
    while ((n=file_read(fd,&byte,1))==1) {
        if (used==sizeof line) return 1;
        line[used++]=byte;
        if (byte!='\n') continue;
        unsigned i=0; while (i<used && argv[2][i] && line[i]==argv[2][i]) ++i;
        if (argv[2][i] || i==used || line[i]!=' ')
            for (i=0;i<used;++i) if (putchar(line[i])<0) return 1;
        used=0;
    }
    file_close(fd); return n<0 || used;
}
