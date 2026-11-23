/* Numeric conversion must use the same binary128 representation in every
   compiler generation. These musl FILEs only ever refer to NUL-ended strings. */
#include <stdio.h>
#include <sys/types.h>
#include <math.h>
void __shlim(FILE *f,off_t limit) { f->shlim=limit; f->shcnt=0; f->shend=f->rend; }
int __shgetc(FILE *f) { (void)f; return EOF; }
long double ldexpl(long double x,int e) { return scalbnl(x,e); }
#ifdef SEED_NEWLIB
extern int *__errno(void);
int *__errno_location(void) { return __errno(); }
#endif
