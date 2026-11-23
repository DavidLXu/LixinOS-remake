/* Page tables and vectors for the standalone TCC-linked reset entry. */
#include "abi.h"
static u64 root_table[512] __attribute__((aligned(4096)));
static u64 ram_table[512] __attribute__((aligned(4096)));
u64 user_pages[512] __attribute__((aligned(4096)));
static u32 vectors[512] __attribute__((aligned(2048)));
extern void user_sync(void),user_timeout(void),exception(void),boot_main(void);
void boot_platform(void) {
    root_table[0]=0x401; root_table[1]=(u64)ram_table|3;
    for (unsigned i=0;i<512;++i) {
        u64 addr=0x40000000UL+i*0x200000UL;
        ram_table[i]=addr|0x705;
        if (addr==0x46000000) ram_table[i]=(u64)user_pages|3;
        if (addr>=0x4e000000 && addr<0x50000000) ram_table[i]=addr|0x600000000007c5UL;
        user_pages[i]=(0x46000000UL+i*4096)|0x60000000000707UL;
    }
    for (unsigned i=0;i<16;++i) {
        void (*fn)(void)=i==8?user_sync:i==9?user_timeout:exception;
        long delta=(long)fn-(long)&vectors[32*i];
        vectors[32*i]=0x14000000|((delta/4)&0x3ffffff);
    }
    extern void install_mmu(void *,void *);
    install_mmu(root_table,vectors);
    boot_main();
}
