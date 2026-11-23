#include "ds_api.h"

static int ds_copy_one_file(const char *src, const char *dest)
{
    DS data;
    int kind;
    int rc;

    kind = ds_kind(src, &data);
    if (kind == 0)
        return ds_fail("source does not exist");
    if (kind == 2)
        return ds_fail("source is a directory");

    rc = ds_change(DS_WRITE, dest, NULL, data, 0);
    return rc;
}

static int ds_do_touch(int argc, char **argv)
{
    DS data;
    int kind;
    int rc;

    if (argc != 2)
        return ds_fail("touch requires exactly one file");

    kind = ds_kind(argv[1], &data);
    if (kind == 1)
        return 0;
    if (kind == 2)
        return ds_fail("target is a directory");

    rc = ds_change(DS_WRITE, argv[1], NULL, (DS){0, 0}, 0);
    return rc;
}

static int ds_do_cp(int argc, char **argv)
{
    if (argc != 3)
        return ds_fail("cp requires source and destination");
    return ds_copy_one_file(argv[1], argv[2]);
}

static int ds_do_mv(int argc, char **argv)
{
    int kind;
    DS data;
    int rc;

    if (argc != 3)
        return ds_fail("mv requires source and destination");

    kind = ds_kind(argv[1], &data);
    if (kind == 0)
        return ds_fail("source does not exist");

    rc = ds_change(DS_MOVE, argv[2], argv[1], (DS){0, 0}, 0);
    return rc;
}

static int ds_do_rm(int argc, char **argv)
{
    int kind;
    DS data;
    int rc;

    if (argc != 2)
        return ds_fail("rm requires exactly one file");

    kind = ds_kind(argv[1], &data);
    if (kind == 0)
        return ds_fail("file does not exist");
    if (kind == 2)
        return ds_fail("target is a directory");

    rc = ds_change(DS_REMOVE, argv[1], NULL, (DS){0, 0}, 0);
    return rc;
}

static int ds_do_mkdir(int argc, char **argv)
{
    int rc;

    if (argc != 2)
        return ds_fail("mkdir requires exactly one directory");

    rc = ds_change(DS_MKDIR, argv[1], NULL, (DS){0, 0}, 0);
    return rc;
}

static int ds_do_rmdir(int argc, char **argv)
{
    int kind;
    DS data;
    int rc;

    if (argc != 2)
        return ds_fail("rmdir requires exactly one directory");

    kind = ds_kind(argv[1], &data);
    if (kind == 0)
        return ds_fail("directory does not exist");
    if (kind == 1)
        return ds_fail("target is a file");

    rc = ds_change(DS_RMDIR, argv[1], NULL, (DS){0, 0}, 0);
    return rc;
}

static int ds_do_tee(int argc, char **argv, DS input)
{
    int append;
    int idx;
    const char *file;
    int rc;

    append = 0;
    idx = 1;

    if (idx < argc && argv[idx][0] == '-' && argv[idx][1] == 'a' && argv[idx][2] == '\0') {
        append = 1;
        idx++;
    }

    if (idx >= argc)
        return ds_fail("tee requires a file");
    if (idx + 1 != argc)
        return ds_fail("tee accepts only one file");

    file = argv[idx];

    if (input.n > 0) {
        if (ds_emit(input.p, input.n) != 0)
            return ds_fail("write failed");
    }

    rc = ds_change(DS_WRITE, file, NULL, input, append);
    return rc;
}

int ds_copy(int argc, char **argv, DS input)
{
    const char *cmd;
    int rc;

    (void)input;

    if (argc < 1)
        return ds_fail("missing command name");

    cmd = argv[0];

    if (ds_eq(cmd, "touch"))
        return ds_do_touch(argc, argv);
    if (ds_eq(cmd, "cp"))
        return ds_do_cp(argc, argv);
    if (ds_eq(cmd, "mv"))
        return ds_do_mv(argc, argv);
    if (ds_eq(cmd, "rm"))
        return ds_do_rm(argc, argv);
    if (ds_eq(cmd, "mkdir"))
        return ds_do_mkdir(argc, argv);
    if (ds_eq(cmd, "rmdir"))
        return ds_do_rmdir(argc, argv);
    if (ds_eq(cmd, "tee"))
        return ds_do_tee(argc, argv, input);

    rc = ds_fail("unknown command");
    return rc;
}
