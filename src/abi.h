/* Stable interface between the resident compiler/ELF loader and each kernel. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;
#define IMAGE_CAP (256 * 1024)
#define SOURCE_CAP (64 * 1024)
#define ABI_VERSION 4
#define SYSTEM_CAP (6U*1024*1024)
struct proposal {
    char read[128], before[6144], after[8192], command[128], expected[256], message[512];
};
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
    int upgrade, trial;
    char diagnostic[1024];
    int (*propose)(const char *url, const char *model, const char *goal,
                   const char *source, const char *feedback, const char *command,
                   const char *expected, int query_only, struct proposal *result);
    int (*probe)(const char *url, const char *model, char *message, unsigned cap);
    void (*end_trial)(void);
    void (*set_key)(const char *url, const char *key);
    const char *tool_seed;
    int (*tool_compile)(const char *source);
    int (*tool_run)(const char *line, const char *kernel, const char *tools, int cwd,
                    const char **text, unsigned *size, int *next_cwd);
    int cwd;
    int (*build_system)(const u8 **image,unsigned *size,int recovery);
    int (*compile_app)(const char *path);
    u8 *system_buffer;
    int guest_resident;
    u32 volume_id;
    void (*boot_system)(const u8 *image,unsigned size);
    int (*compile_apps)(const char *tools);
};
