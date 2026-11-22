#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_DNS 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_IPV6 0
#define LWIP_STATS 0
#define LWIP_DHCP 0
#define LWIP_AUTOIP 0
#define MEM_ALIGNMENT 8
#define MEM_SIZE (128 * 1024)
#define PBUF_POOL_SIZE 32
#define TCP_MSS 1460
#define TCP_SND_BUF (8 * TCP_MSS)
#define TCP_WND (16 * TCP_MSS)
#define MEMP_NUM_TCP_SEG 40
#define LWIP_NOASSERT 1
#define SYS_LIGHTWEIGHT_PROT 0
