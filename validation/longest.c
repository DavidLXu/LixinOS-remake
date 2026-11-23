#include "count-table.h"
int main(int argc,char **argv) {
    if (argc!=3 || !read_text(argv[1],rows,sizeof rows) || !read_text(argv[2],answer,sizeof answer))
        return fail("unreadable result files","");
    struct counts_summary s;
    if (checked_counts(rows,&s)) return 1;
    char *p=answer,*winner; unsigned count;
    if (!row(&p,&winner,&count) || *p || compare(winner,s.winner) || count!=s.largest)
        return fail("incorrect maximum","");
    puts("complete counts and maximum verified"); return 0;
}
