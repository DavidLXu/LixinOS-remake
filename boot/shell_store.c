/* Both snapshots occupy the unused tail of system bank A, above the maximum
   6 MiB resident ELF and below the first sector of system bank B. */
#define SHELL_STORE_BASE (2048U + 1U + SYSTEM_CAP / 512U)
#define SHELL_STORE_BANK 272U
struct shell_tree {
    struct {
        char name[48], text[4096];
        unsigned installed;
    } files[SHELL_FILES];
};
static struct shell_tree shell_files, shell_next;
static unsigned shell_revision;
typedef char shell_store_fits[(sizeof shell_files <= (SHELL_STORE_BANK - 1) * 512 &&
                               SHELL_STORE_BASE + 2 * SHELL_STORE_BANK <= 2048 + 16384)
                                  ? 1
                                  : -1];
static int shell_index(const char *path) {
    if (strncmp(path, "/work/", 6) || !path[6])
        return -1;
    for (unsigned i = 0; i < SHELL_FILES; ++i)
        if (!strcmp(path + 6, shell_files.files[i].name))
            return i;
    return -1;
}
static void load_shell_store(unsigned sectors) {
    if (sectors < SHELL_STORE_BASE + 2 * SHELL_STORE_BANK)
        return;
    for (unsigned bank = 0; bank < 2; ++bank) {
        unsigned base = SHELL_STORE_BASE + bank * SHELL_STORE_BANK;
        struct work_stamp h;
        if (!package_io(0, base, (u8 *)&h, sizeof h) || h.magic != 0x53484631 ||
            (h.revision & 1) != bank || h.revision <= shell_revision || !matches(&h, 40, h.guard) ||
            !package_io(0, base + 1, (u8 *)&shell_next, sizeof shell_next) ||
            !matches(&shell_next, sizeof shell_next, h.payload))
            continue;
        shell_files = shell_next;
        shell_revision = h.revision;
    }
}
static int save_shell_store(unsigned sectors) {
    struct work_stamp h = {0x53484631, shell_revision + 1};
    if (!h.revision || sectors < SHELL_STORE_BASE + 2 * SHELL_STORE_BANK)
        return 0;
    unsigned base = SHELL_STORE_BASE + (h.revision & 1) * SHELL_STORE_BANK;
    sha(&shell_next, sizeof shell_next, h.payload);
    sha(&h, 40, h.guard);
    if (!package_io(1, base + 1, (u8 *)&shell_next, sizeof shell_next) || !package_flush() ||
        !package_io(1, base, (u8 *)&h, sizeof h) || !package_flush())
        return 0;
    shell_files = shell_next;
    shell_revision = h.revision;
    return 1;
}
