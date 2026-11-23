/* Shared with the single foreground EL0 tool. No kernel pointers cross the ABI. */
struct tool_input {
    int cwd;
    char command[2048], kernel[65536], tools[16384];
    char program[192];
};
struct tool_output {
    unsigned length, truncated;
    int cwd;
    char text[98304];
};
