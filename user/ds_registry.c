#include "ds_api.h"

int ds_basic(int,char **,DS);
int ds_format(int,char **,DS);
int ds_paths(int,char **,DS);
int ds_catalog(int,char **,DS);
int ds_copy(int,char **,DS);
int ds_inspect(int,char **,DS);
int ds_catwc(int,char **,DS);
int ds_ends(int,char **,DS);
int ds_grep(int,char **,DS);
int ds_sorting(int,char **,DS);
int ds_fields(int,char **,DS);
int ds_translate(int,char **,DS);
int ds_catalogfs(int,char **,DS);
int ds_search(int,char **,DS);
int ds_edit(int,char **,DS);
int ds_compare(int,char **,DS);
int ds_digest(int,char **,DS);
int ds_arguments(int,char **,DS);

static const struct { const char *name; int (*run)(int,char **,DS); } applets[] = {
    {"echo",ds_basic},
    {"true",ds_basic},
    {"false",ds_basic},
    {"seq",ds_basic},
    {"printf",ds_format},
    {"basename",ds_paths},
    {"dirname",ds_paths},
    {"pwd",ds_paths},
    {"cd",ds_paths},
    {"which",ds_catalog},
    {"commands",ds_catalog},
    {"touch",ds_copy},
    {"cp",ds_copy},
    {"mv",ds_copy},
    {"rm",ds_copy},
    {"mkdir",ds_copy},
    {"rmdir",ds_copy},
    {"tee",ds_copy},
    {"stat",ds_inspect},
    {"cmp",ds_inspect},
    {"test",ds_inspect},
    {"cat",ds_catwc},
    {"wc",ds_catwc},
    {"head",ds_ends},
    {"tail",ds_ends},
    {"nl",ds_ends},
    {"grep",ds_grep},
    {"sort",ds_sorting},
    {"uniq",ds_sorting},
    {"cut",ds_fields},
    {"awk",ds_fields},
    {"tr",ds_translate},
    {"ls",ds_catalogfs},
    {"find",ds_catalogfs},
    {"rg",ds_search},
    {"sed",ds_edit},
    {"diff",ds_compare},
    {"sha256sum",ds_digest},
    {"xargs",ds_arguments},
};

int ds_invoke(int argc,char **argv,DS input){
    for(unsigned i=0;i<sizeof applets/sizeof *applets;++i)
        if(argc && ds_eq(argv[0],applets[i].name))return applets[i].run(argc,argv,input);
    return ds_fail("unknown deepseek command");
}
