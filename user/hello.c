#include "app.h"
int main(int argc,char **argv) {
    const char *greeting="hello ";
    while (*greeting) putchar(*greeting++);
    puts(argc>1?argv[1]:"guest");
    return 0;
}
