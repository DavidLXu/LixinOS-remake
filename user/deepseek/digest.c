#include "ds_api.h"

static int digest_one(const char *name, DS input, DS *data, int is_stdin)
{
  char hex[65];
  const char *label;
  unsigned ln;
  int n;

  if (is_stdin) {
    *data = input;
    label = "-";
  } else {
    if (ds_input(name, input, data) != 0)
      return ds_fail("cannot read input");
    label = name;
  }

  ds_hash(*data, hex);
  if (ds_emit(hex, 64) != 0) return 1;
  if (ds_emit("  ", 2) != 0) return 1;

  ln = 0;
  while (label[ln]) ln++;
  if (ds_emit(label, ln) != 0) return 1;
  if (ds_emit("\n", 1) != 0) return 1;

  (void)n;
  return 0;
}

int ds_digest(int argc, char **argv, DS input)
{
  DS data;
  int rc;

  if (argc < 1) return ds_fail("missing argv[0]");

  if (argc == 1) {
    data = input;
    return digest_one(NULL, input, &data, 1);
  }

  {
    int i;
    int first = 1;
    for (i = 1; i < argc; i++) {
      const char *name = argv[i];
      int is_stdin = 0;

      if (name[0] == '-' && name[1] == 0) {
        is_stdin = 1;
      } else if (name[0] == '-' && name[1] != 0) {
        return ds_fail("unknown option");
      }

      if (!first) {
        if (ds_emit("\n", 0) != 0) return 1;
      }
      first = 0;

      {
        DS d;
        char hex[65];
        unsigned ln;

        if (is_stdin) d = input;
        else if (ds_input(name, input, &d) != 0)
          return ds_fail("cannot read input");

        ds_hash(d, hex);
        if (ds_emit(hex, 64) != 0) return 1;
        if (ds_emit("  ", 2) != 0) return 1;

        {
          const char *label = is_stdin ? "-" : name;
          ln = 0;
          while (label[ln]) ln++;
          if (ds_emit(label, ln) != 0) return 1;
        }
        if (ds_emit("\n", 1) != 0) return 1;
      }
    }
  }

  (void)rc;
  return 0;
}
