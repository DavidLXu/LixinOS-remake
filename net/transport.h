#include <stddef.h>
int transport_connect(const char *host, unsigned port, char *error, size_t cap);
void transport_close(void);
int transport_write(void *ctx, const unsigned char *p, size_t n);
int transport_read(void *ctx, unsigned char *p, size_t n);
int guest_entropy(unsigned char *out, size_t len);
