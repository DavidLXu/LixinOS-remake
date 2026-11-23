#include "/src/validation/count-table.h"
int main(int argc,char **argv) {
    if (argc!=3 || !read_text(argv[1],rows,sizeof rows) || !read_text(argv[2],answer,sizeof answer))
        return fail("unreadable result files","");
    struct counts_summary counts;
    if (checked_counts(rows,&counts)) return 1;
    char *p=answer;
    if (*p<'0' || *p>'9') return fail("malformed total","");
    unsigned value=0;
    while (*p>='0' && *p<='9') {
        unsigned digit=*p++-'0';
        if (value>(0xffffffffU-digit)/10) return fail("total overflow","");
        value=value*10+digit;
    }
    if (*p!='\n' || p[1] || value!=counts.total) return fail("incorrect total","");
    puts("complete counts and total verified"); return 0;
}
