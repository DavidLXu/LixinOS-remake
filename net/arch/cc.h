#include <stdint.h>
#include <stdlib.h>
#define LWIP_RAND() net_random()
unsigned int net_random(void);
#ifndef SSIZE_MAX
#define SSIZE_MAX LONG_MAX
#endif
