#ifndef LIXINOS_APP_H
#define LIXINOS_APP_H
int putchar(int c);
int puts(const char *s);
int file_open(const char *path);
int file_read(int fd, void *buffer, unsigned bytes);
int file_close(int fd);
int main(int argc,char **argv);
#endif
