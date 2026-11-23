/* The broker commits one validated mutation after a successful EL0 pipeline. */
#include "commands.h"
#define SHELL_NAME(name, usage) #name,
static const char *shell_names[] = {SHELL_COMMANDS(SHELL_NAME)};
#define SHELL_USAGE(name, usage) #name " " usage "\n"
static const char shell_menu[] = SHELL_COMMANDS(SHELL_USAGE);
static u8 shell_elf[IMAGE_CAP];
static unsigned shell_size;
static const char *cached_shell;
static int shell_known(const char *name) {
    for (unsigned i = 0; i < sizeof shell_names / sizeof *shell_names; ++i)
        if (!strcmp(name, shell_names[i]))
            return 1;
    return 0;
}
static int protected_leaf(const char *path) {
    if (!strcmp(path, "/work/counts.txt") || !strcmp(path, "/work/answer.txt") ||
        !strcmp(path, "/work/scratch.txt"))
        return 1;
    if (module_path(path) || work_index(path) >= 0 || entry_file(&pending, path))
        return 1;
    for (unsigned i = 0; i < APP_COUNT; ++i)
        if (entry_file(entries + i, path))
            return 1;
    return 0;
}
static int writable_path(const char *path) {
    if (strncmp(path, "/work/", 6) || !path[6] ||
        strlen(path + 6) >= sizeof shell_files.files[0].name || protected_leaf(path))
        return 0;
    const char *part = path + 6;
    for (const char *p = part;; ++p) {
        if (!*p || *p == '/') {
            int len = p - part;
            if (!len || (len == 1 && *part == '.') || (len == 2 && !memcmp(part, "..", 2)))
                return 0;
            if (!*p)
                break;
            char parent[64];
            memcpy(parent, path, p - path);
            parent[p - path] = 0;
            int i = shell_index(parent);
            if (i < 0 || shell_files.files[i].installed != WORK_DIRECTORY)
                return 0;
            part = p + 1;
        } else if ((unsigned char)*p < 32 || *p == 127)
            return 0;
    }
    return 1;
}
static const char *commit_shell(struct tool_output *out, unsigned sectors) {
    if (!memchr(out->target, 0, sizeof out->target) ||
        !memchr(out->origin, 0, sizeof out->origin) ||
        !memchr(out->payload, 0, sizeof out->payload) || !writable_path(out->target))
        return "invalid or protected destination";
    int i = shell_index(out->target);
    unsigned op = out->change;
    shell_next = shell_files;
    if (i >= 0 && shell_files.files[i].installed < WORK_WRITABLE)
        return "immutable workspace input";
    if (op == SH_REMOVE || op == SH_RMDIR) {
        if (i < 0)
            return "path not found";
        if ((op == SH_RMDIR) != (shell_files.files[i].installed == WORK_DIRECTORY))
            return "wrong file type";
        const char *target = shell_files.files[i].name;
        unsigned len = strlen(target);
        if (op == SH_RMDIR) {
            if (!strcmp(shell_directory, out->target))
                return "directory is current";
            for (unsigned j = 0; j < SHELL_FILES; ++j)
                if (!strncmp(shell_files.files[j].name, target, len) &&
                    shell_files.files[j].name[len] == '/')
                    return "directory not empty";
        }
        memset(shell_next.files + i, 0, sizeof shell_next.files[i]);
    } else if (op == SH_MOVE) {
        if (!writable_path(out->origin))
            return "invalid source";
        int src = shell_index(out->origin);
        if (src < 0 || shell_files.files[src].installed != WORK_WRITABLE)
            return "mutable source file required";
        if (i >= 0 && shell_files.files[i].installed != WORK_WRITABLE)
            return "destination is a directory";
        if (i == src)
            return NULL;
        if (i >= 0)
            memset(shell_next.files + i, 0, sizeof shell_next.files[i]);
        strcpy(shell_next.files[src].name, out->target + 6);
    } else if (op == SH_WRITE || op == SH_MKDIR) {
        if (i >= 0 && (op == SH_MKDIR || shell_files.files[i].installed == WORK_DIRECTORY))
            return "destination already exists as a directory or file";
        if (i < 0)
            for (unsigned j = 0; j < SHELL_FILES; ++j)
                if (!shell_files.files[j].name[0]) {
                    i = j;
                    break;
                }
        if (i < 0)
            return "workspace full";
        strcpy(shell_next.files[i].name, out->target + 6);
        strcpy(shell_next.files[i].text, out->payload);
        shell_next.files[i].installed = op == SH_MKDIR ? WORK_DIRECTORY : WORK_WRITABLE;
    } else
        return "unknown operation";
    return save_shell_store(sectors) ? NULL : "workspace commit failed";
}
static int run_shell(const char *line, const char *tools, unsigned sectors) {
    const char *build="/src/user/shell.build";
    if(!strncmp(line,"dsh local ",10)){line+=10;build="/work/dsh.build";}
    else if(!strncmp(line,"dsh ",4)){line+=4;build="/src/user/deepseek.build";}
    if (!shell_size || strcmp(build,cached_shell)) {
        shell_size=0;
        if (!compile_app(build)) {
            puts("shell build failed");
            return 2;
        }
        cached_shell=build;
        shell_size = tool_size;
        memcpy(shell_elf, tool_image, tool_size);
    } else {
        tool_size = shell_size;
        memcpy(tool_image, shell_elf, shell_size);
        if (!load_app(build))
            return 2;
    }
    const char *text = NULL;
    unsigned n = 0;
    int cwd = machine.cwd;
    int result = tool_run(line, machine.source, tools, cwd, &text, &n, &cwd);
    if (result && result != -4) {
        puts("shell execution failed");
        return 2;
    }
    struct tool_output *out = (void *)(USER_BASE + 0x180000);
    if (!result && out->change) {
        const char *msg = commit_shell(out, sectors);
        if (msg) {
            printf("sh: %s\nexit=2\n", msg);
            return 2;
        }
    }
    if (!result && memchr(out->shell_dir, 0, sizeof out->shell_dir)) {
        if (!out->shell_dir[0]) {
            machine.cwd = cwd;
            shell_directory[0] = 0;
        } else {
            int i = shell_index(out->shell_dir);
            if (i >= 0 && shell_files.files[i].installed == WORK_DIRECTORY) {
                machine.cwd = cwd;
                strcpy(shell_directory, out->shell_dir);
            }
        }
    }
    if (text)
        _write(1, text, n);
    if (result)
        printf("exit=%d\n", out->status);
    return 2;
}
static int has_syntax(const char *p, const char *set) {
    while (*p)
        if (strchr(set, *p++))
            return 1;
    return 0;
}
static int shell_dispatch(const char *cmd, const char *tools, unsigned sectors) {
    if (!strncmp(cmd,"dsh ",4)) return run_shell(cmd,tools,sectors);
    if (!strcmp(cmd,"dsh")) return run_shell("dsh commands",tools,sectors);
    if (!strncmp(cmd, "sh ", 3))
        return run_shell(cmd + 3, tools, sectors);
    if (!strcmp(cmd, "sh")) {
        puts(shell_menu);
        return 1;
    }
    char name[32];
    unsigned k = 0;
    while (cmd[k] && !strchr(" \t|<>", cmd[k]) && k + 1 < sizeof name) {
        name[k] = cmd[k];
        ++k;
    }
    name[k] = 0;
    int installed=app_index(name);
    if(installed>=0 && strchr(cmd,'|')){
        const char *sep=strchr(cmd,'|');unsigned len=sep-(cmd+k);
        char args[2048];if(len>=sizeof args)return 0;
        memcpy(args,cmd+k,len);args[len]=0;
        struct capture capture={shell_pipe.text,0,sizeof shell_pipe.text};
        if(!execute_app(installed,args,tools,0,&capture)){puts("pipeline producer failed");return 2;}
        shell_pipe.size=capture.used;
        int rc=run_shell(sep+1,tools,sectors);
        shell_pipe.size=0;return rc;
    }
    if (!shell_known(name))
        return 0;
    if (app_index(name) >= 0 && !has_syntax(cmd, "|<>"))
        return 0;
    int legacy = !strcmp(name, "pwd") || !strcmp(name, "cd") || !strcmp(name, "ls") ||
                 !strcmp(name, "cat") || !strcmp(name, "grep");
    if (legacy && !shell_directory[0] && !has_syntax(cmd, "|\"'<>") && !strstr(cmd, " -")) {
        if (!strcmp(name, "cat") && cmd[k] && strchr(cmd + k + 1, ' '))
            return run_shell(cmd, tools, sectors);
        int work = machine.cwd == source_lookup(0, "/work") || strstr(cmd, "/work") != NULL;
        if (!work)
            return 0;
        int managed = 0;
        for (unsigned i = 0; i < SHELL_FILES; ++i)
            if (shell_files.files[i].installed >= WORK_WRITABLE)
                managed = 1;
        if (!managed)
            return 0;
    }
    return run_shell(cmd, tools, sectors);
}
