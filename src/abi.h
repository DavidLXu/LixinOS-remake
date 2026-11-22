/* Stable interface between the resident compiler/ELF loader and each kernel. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;
#define IMAGE_CAP (256 * 1024)
#define SOURCE_CAP (64 * 1024)
#define ABI_VERSION 1
struct machine {
    u32 abi;
    int slot;
    u64 epoch;
    u32 switches;
    char note[96];
    u32 image_len, source_len;
    u8 image[IMAGE_CAP];
    char source[SOURCE_CAP];
    int (*compile)(struct machine *, unsigned);
    int (*valid)(const u8 *, u32);
    void (*enter)(struct machine *);
    void (*barrier)(void);
    void (*poweroff)(void);
};
