/* Shared with the single foreground EL0 tool. No kernel pointers cross the ABI. */
struct tool_input {
    int cwd;
    char command[2048], kernel[65536], tools[16384];
    char program[192];
    unsigned work_count;
    struct { char name[32], code[8192], recipe[128]; } work[5];
    char counts[8192], answer[320], scratch[8192];
    unsigned has_scratch;
#define SHELL_FILES 32
#define VISIBLE_WORK_FILES (8 + SHELL_FILES)
    struct { char name[48], text[4096]; unsigned installed; } files[VISIBLE_WORK_FILES];
    char shell_dir[64];
    unsigned pipe_bytes;
    char pipe_data[98304];
};
struct tool_output {
    unsigned length, truncated;
    int cwd;
    char text[98304];
    int status;
    unsigned change;
    char target[64], origin[64], payload[4096], shell_dir[64];
};

/* Resident publishes the snapshotted applet source namespace read-only at EL0. */
#define APPLET_COUNT 24
struct applet_source { char group[32],code[8192],usage[256]; };
struct applet_view { struct applet_source entries[APPLET_COUNT]; char recipe[4096]; };
#define USER_APPLETS ((struct applet_view *)0x461a0000UL)
