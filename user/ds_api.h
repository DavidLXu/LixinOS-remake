#ifndef DS_API_H
#define DS_API_H
#include <stddef.h>
typedef struct stream {const char *p; unsigned n;} DS;
/* Implement int ds_GROUP(int argc,char **argv,DS input); argv[0]=command.
   Return 0 on success, 1 for a negative predicate, ds_fail("message") for misuse.
   No libc. Data spans may not be NUL terminated. Do not modify argv or input. */
unsigned ds_len(const char *s);
int ds_eq(const char *a,const char *b);
int ds_num(const char *s,int *v); /* 1 for a valid signed decimal */
int ds_emit(const char *data,unsigned n); /* 0 success */
void ds_put(const char *s);
void ds_number(int value);
int ds_fail(const char *message);
int ds_path(const char *name,char absolute[256]); /* 0 success */
int ds_kind(const char *path,DS *data); /* 0 missing, 1 file (bytes), 2 directory */
int ds_input(const char *name,DS pipe,DS *data); /* NULL or "-" = pipe; 0 success */
const char *ds_cwd(void);
int ds_setcwd(const char *path);
typedef void (*ds_visit)(const char *absolute,int kind,void *ctx);
void ds_walk(ds_visit visit,void *ctx); /* every visible absolute path once, root included */
int ds_glob(const char *pattern,const char *leaf); /* * and ?; 1 matches */
int ds_line(DS data,unsigned *pos,DS *line); /* advances pos, excludes newline */
int ds_cmp(DS a,DS b); /* lexicographic */
int ds_match(DS line,const char *literal,int insensitive);
enum {DS_WRITE=1,DS_MOVE,DS_REMOVE,DS_MKDIR,DS_RMDIR};
/* Queue one durable change. For MOVE: destination then source; else source=NULL.
   Only DS_WRITE takes data. append=1 preserves preceding file bytes. */
int ds_change(int op,const char *dest,const char *source,DS data,int append);
void ds_hash(DS data,char hex[65]); /* SHA-256 lowercase NUL-terminated */
int ds_count(void);
const char *ds_name(int i);
const char *ds_usage(int i);
int ds_invoke(int argc,char **argv,DS input); /* invoke another generated command, append stdout */
#endif
