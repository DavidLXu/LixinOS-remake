#ifndef LX_STDIO_H
#define LX_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct lx_file { int fd; unsigned char *buf,*rpos,*rend,*shend; long shcnt,shlim; } FILE;
extern FILE *stdin,*stdout,*stderr;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
FILE *fdopen(int,const char *);
int fclose(FILE *), fflush(FILE *), fputc(int,FILE *), fputs(const char *,FILE *);
size_t fwrite(const void *,size_t,size_t,FILE *);
int vfprintf(FILE *,const char *,va_list);
int printf(const char *,...), fprintf(FILE *,const char *,...), puts(const char *), putchar(int);
int sprintf(char *,const char *,...), snprintf(char *,size_t,const char *,...);
int vsnprintf(char *,size_t,const char *,va_list), sscanf(const char *,const char *,...);
#endif
