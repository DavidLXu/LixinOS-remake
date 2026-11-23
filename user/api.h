/* Shared with the single foreground EL0 tool. No kernel pointers cross the ABI. */
struct tool_input {
    int cwd;
    char command[2048], kernel[65536], tools[16384];
    char program[192];
    unsigned work_count;
    struct { char name[32], code[8192], recipe[128]; } work[5];
    char counts[8192], answer[320], scratch[8192];
    unsigned has_scratch;
    struct { char name[48], text[4096]; unsigned installed; } files[8];
};
struct tool_output {
    unsigned length, truncated;
    int cwd;
    char text[98304];
    int status;
};
