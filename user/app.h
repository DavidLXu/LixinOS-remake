#ifndef LIXINOS_APP_H
#define LIXINOS_APP_H
int putchar(int c);
int putstr(const char *s); /* Write the characters without a newline. */
int puts(const char *s);   /* Write the characters and append one newline. */
int file_open(const char *path);
int file_read(int fd, void *buffer, unsigned bytes);
int file_close(int fd);
int main(int argc,char **argv);
#endif
