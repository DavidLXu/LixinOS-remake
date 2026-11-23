#include "ds_api.h"

static int emit_name(const char *s)
{
	unsigned n = ds_len(s);
	if (ds_emit(s, n) != 0) return 1;
	if (ds_emit("\n", 1) != 0) return 1;
	return 0;
}

static int emit_entry(int i)
{
	ds_put(ds_name(i));
	ds_put(" ");
	ds_put(ds_usage(i));
	ds_put("\n");
	return 0;
}

static int do_commands(int argc, char **argv)
{
	int i, n;
	if (argc > 2) return ds_fail("too many operands");
	n = ds_count();
	if (argc == 2) {
		for (i = 0; i < n; i++) {
			if (ds_eq(argv[1], ds_name(i))) return emit_entry(i);
		}
		return 1;
	}
	for (i = 0; i < n; i++) {
		if (emit_entry(i) != 0) return 1;
	}
	return 0;
}

static int do_which(int argc, char **argv)
{
	int i, n;
	if (argc < 2) return ds_fail("missing operand");
	if (argc > 2) return ds_fail("too many operands");
	n = ds_count();
	for (i = 0; i < n; i++) {
		if (ds_eq(argv[1], ds_name(i))) {
			if (emit_name(ds_name(i)) != 0) return 1;
			return 0;
		}
	}
	return 1;
}

int ds_catalog(int argc, char **argv, DS input)
{
	(void)input;
	if (argc < 1) return ds_fail("missing command");
	if (ds_eq(argv[0], "commands")) return do_commands(argc, argv);
	if (ds_eq(argv[0], "which")) return do_which(argc, argv);
	return ds_fail("unknown command");
}
