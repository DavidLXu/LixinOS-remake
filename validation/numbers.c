#include "app.h"
#include <stdint.h>
static const union { long double number; uint64_t words[2]; } wide[]={
    {79228162514264337593543950336.0L}, {0x1.0000000000000000000000000001p0L},
    {0x1.ffffffffffffffffffffffffffffp16383L}
};
static const union { double number; uint64_t bits; } narrow[]={ {0.1}, {0x1p-1074} };
int main(int argc,char **argv) {
    (void)argc; (void)argv;
    int ok=wide[0].words[0]==0 && wide[0].words[1]==0x405f000000000000ULL &&
        wide[1].words[0]==1 && wide[1].words[1]==0x3fff000000000000ULL &&
        wide[2].words[0]==~0ULL && wide[2].words[1]==0x7ffeffffffffffffULL &&
        narrow[0].bits==0x3fb999999999999aULL && narrow[1].bits==1;
    puts(ok?"floating constants verified":"floating constants differ");
    return !ok;
}
