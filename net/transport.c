/* Synchronous guest TCP stream over the QEMU virt user-network. */
#include "transport.h"
#include "lwip/init.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef volatile uint32_t *mmio;
enum { QSIZE = 8, FRAME = 1600, NET_HEADER = 12 };
struct desc { uint64_t addr; uint32_t len; uint16_t flags, next; };
struct queue {
    struct desc desc[QSIZE] __attribute__((aligned(16)));
    volatile struct { uint16_t flags, idx, id[QSIZE], event; } avail;
    volatile struct { uint16_t flags, idx; struct { uint32_t id, len; } elem[QSIZE]; uint16_t event; } used;
    uint16_t seen;
};
static struct queue rx, tx, rng;
static mmio nic, random_device;
static unsigned char packets[QSIZE][FRAME], outbound[FRAME], randomness[64];
static struct netif interface;
static struct tcp_pcb *stream;
static int initialized, connected, ended, resolved;
static uintptr_t dns_attempt;
static ip_addr_t answer;
static unsigned char inbound[32768];
static unsigned read_pos, count;
static u32_t deadline;

u32_t sys_now(void) {
    uint64_t tick, freq;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tick));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return tick / (freq / 1000);
}
static int expired(void) { return (int32_t)(sys_now() - deadline) >= 0; }
static void barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static mmio device(unsigned id) {
    for (int i = 0; i < 32; i++) {
        mmio r = (mmio)(0x0a000000UL + i * 512);
        if (r[0] == 0x74726976 && r[1] == 2 && r[2] == id) return r;
    }
    return NULL;
}
static int negotiate(mmio r, uint32_t bits) {
    if (!r) return 0;
    r[28] = 0; while (r[28]) {}
    r[28] = 3; r[5] = 0;
    if ((r[4] & bits) != bits) return 0;
    r[9] = 0; r[8] = bits;
    r[5] = 1; if (!(r[4] & 1)) return 0;
    r[9] = 1; r[8] = 1; r[28] = 11;
    return (r[28] & 8) != 0;
}
static int attach(mmio r, unsigned index, struct queue *q) {
    r[12] = index;
    if (r[13] < QSIZE) return 0;
    memset(q, 0, sizeof *q); q->avail.flags = 1;
    r[14] = QSIZE;
    r[32] = (uintptr_t)q->desc; r[33] = 0;
    r[36] = (uintptr_t)&q->avail; r[37] = 0;
    r[40] = (uintptr_t)&q->used; r[41] = 0;
    r[17] = 1;
    return 1;
}
static void post(mmio r, unsigned index, struct queue *q, unsigned id, void *p, unsigned n, int input) {
    q->desc[id].addr = (uintptr_t)p; q->desc[id].len = n; q->desc[id].flags = input ? 2 : 0;
    q->avail.id[q->avail.idx % QSIZE] = id;
    barrier(); ++q->avail.idx; barrier(); r[20] = index;
}
int guest_entropy(unsigned char *out, size_t n) {
    if (!random_device || n > sizeof randomness) return 0;
    post(random_device, 0, &rng, 0, randomness, n, 1);
    u32_t start = sys_now();
    while (rng.seen == rng.used.idx)
        if (sys_now() - start > 3000) return 0;
    barrier();
    unsigned got = rng.used.elem[rng.seen++ % QSIZE].len;
    if (got != n) return 0;
    memcpy(out, randomness, n); return 1;
}
unsigned net_random(void) {
    unsigned value;
    if (!guest_entropy((unsigned char *)&value, sizeof value)) return sys_now();
    return value;
}
static err_t send_frame(struct netif *n, struct pbuf *p) {
    (void)n;
    if (p->tot_len + NET_HEADER > FRAME || tx.seen != tx.used.idx) return ERR_IF;
    memset(outbound, 0, NET_HEADER);
    pbuf_copy_partial(p, outbound + NET_HEADER, p->tot_len, 0);
    post(nic, 1, &tx, 0, outbound, NET_HEADER + p->tot_len, 0);
    u32_t start = sys_now();
    while (tx.seen == tx.used.idx)
        if (sys_now() - start > 3000) return ERR_IF;
    barrier(); ++tx.seen; return ERR_OK;
}
static err_t init_interface(struct netif *n) {
    n->name[0] = 'v'; n->name[1] = 'n'; n->mtu = 1500; n->hwaddr_len = 6;
    for (int i = 0; i < 6; ++i) n->hwaddr[i] = ((volatile unsigned char *)nic)[256+i];
    n->flags = NETIF_FLAG_ETHARP | NETIF_FLAG_BROADCAST | NETIF_FLAG_LINK_UP;
    n->output = etharp_output; n->linkoutput = send_frame;
    return ERR_OK;
}
static int init_network(void) {
    nic = device(1); random_device = device(4);
    if (!negotiate(random_device, 0) || !attach(random_device, 0, &rng)) return 0;
    random_device[28] = 15;
    if (!negotiate(nic, 1 << 5) || !attach(nic, 0, &rx) || !attach(nic, 1, &tx)) return 0;
    nic[28] = 15;
    for (unsigned i = 0; i < QSIZE; i++) post(nic, 0, &rx, i, packets[i], FRAME, 1);
    lwip_init();
    ip4_addr_t ip, mask, gateway;
    IP4_ADDR(&ip,10,0,2,15); IP4_ADDR(&mask,255,255,255,0); IP4_ADDR(&gateway,10,0,2,2);
    if (!netif_add(&interface,&ip,&mask,&gateway,NULL,init_interface,ethernet_input)) return 0;
    netif_set_default(&interface); netif_set_up(&interface);
    ip_addr_t dns; IP_ADDR4(&dns,10,0,2,3); dns_setserver(0,&dns);
    return initialized = 1;
}
static void poll_net(void) {
    while (rx.seen != rx.used.idx) {
        barrier();
        unsigned i = rx.seen++ % QSIZE, id = rx.used.elem[i].id, n = rx.used.elem[i].len;
        if (id >= QSIZE) continue;
        if (n > NET_HEADER && n <= FRAME) {
            struct pbuf *p = pbuf_alloc(PBUF_RAW, n-NET_HEADER, PBUF_RAM);
            if (p) {
                pbuf_take(p, packets[id]+NET_HEADER, n-NET_HEADER);
                if (interface.input(p,&interface) != ERR_OK) pbuf_free(p);
            }
        }
        post(nic, 0, &rx, id, packets[id], FRAME, 1);
    }
    sys_check_timeouts();
}
static err_t received(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t error) {
    (void)arg; (void)pcb;
    if (!p) { ended = 1; return ERR_OK; }
    if (error != ERR_OK || p->tot_len > sizeof inbound - count) return ERR_MEM;
    unsigned offset = (read_pos + count) % sizeof inbound;
    unsigned first = sizeof inbound - offset;
    if (first > p->tot_len) first = p->tot_len;
    pbuf_copy_partial(p,inbound+offset,first,0);
    pbuf_copy_partial(p,inbound,p->tot_len-first,first);
    count += p->tot_len; pbuf_free(p); return ERR_OK;
}
static void lost(void *arg, err_t error) { (void)arg; (void)error; stream = NULL; ended = 1; }
static err_t opened(void *arg, struct tcp_pcb *pcb, err_t error) {
    (void)arg; (void)pcb; connected = error == ERR_OK; return error;
}
static void found(const char *name, const ip_addr_t *ip, void *arg) {
    (void)name; if ((uintptr_t)arg != dns_attempt) return;
    resolved = ip ? 1 : -1; if (ip) answer = *ip;
}
void transport_close(void) {
    if (stream) { tcp_err(stream,NULL); tcp_abort(stream); stream=NULL; }
}
int transport_connect(const char *host, unsigned port, char *error, size_t cap) {
    if (!initialized && !init_network()) { snprintf(error,cap,"VirtIO network/RNG unavailable"); return 0; }
    transport_close(); count = read_pos = connected = ended = resolved = 0;
    deadline = sys_now() + 15000;
    err_t e = dns_gethostbyname(host,&answer,found,(void *)++dns_attempt);
    if (e == ERR_INPROGRESS) { while (!resolved && !expired()) poll_net(); }
    if (e != ERR_OK && resolved != 1) { snprintf(error,cap,"DNS lookup failed"); return 0; }
    stream = tcp_new();
    if (!stream) { snprintf(error,cap,"TCP allocation failed"); return 0; }
    tcp_recv(stream,received); tcp_err(stream,lost);
    e = tcp_connect(stream,&answer,port,opened);
    while (e == ERR_OK && !connected && !ended && !expired()) poll_net();
    if (!connected) { transport_close(); snprintf(error,cap,"TCP connect failed"); return 0; }
    deadline = sys_now() + 120000;
    return 1;
}
int transport_read(void *ctx, unsigned char *p, size_t n) {
    (void)ctx;
    while (!count && !ended && !expired()) poll_net();
    if (!count) return -1;
    if (n > count) n = count;
    if (n > sizeof inbound - read_pos) n = sizeof inbound - read_pos;
    memcpy(p,inbound+read_pos,n); read_pos = (read_pos+n)%sizeof inbound; count -= n;
    if (stream) tcp_recved(stream,n);
    return n;
}
int transport_write(void *ctx, const unsigned char *p, size_t n) {
    (void)ctx;
    while (stream && !ended && !expired()) {
        poll_net();
        if (!stream) break;
        unsigned space = tcp_sndbuf(stream);
        if (!space) continue;
        unsigned send = n < space ? n : space; if (send > 4096) send = 4096;
        err_t err = tcp_write(stream,p,send,TCP_WRITE_FLAG_COPY);
        if (err == ERR_MEM) continue;
        if (err != ERR_OK) break;
        tcp_output(stream); return send;
    }
    return -1;
}
