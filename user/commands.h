/* One catalog for the guest console, agent discovery, and the EL0 shell. */
#define SHELL_COMMANDS(X) \
 X(basename,"PATH [SUFFIX]") X(dirname,"PATH") X(echo,"[-n] [TEXT ...]") \
 X(printf,"FORMAT [TEXT ...] (%s and backslash escapes)") X(seq,"[FIRST [STEP]] LAST") \
 X(cat,"[FILE ...]") X(grep,"[-invFc] PATTERN [FILE] (literal match)") \
 X(wc,"[-lwc] [FILE]") X(head,"[-n N] [FILE]") X(tail,"[-n N] [FILE]") \
 X(sort,"[-ru] [FILE]") X(uniq,"[-c] [FILE]") X(nl,"[FILE]") \
 X(cut,"-d CHAR -f N [FILE]") X(tr,"[-d] SET [SET]") \
 X(pwd,"") X(cd,"[DIR]") X(ls,"[PATH]") X(find,"[PATH] [-type f|d] [-name GLOB]") \
 X(stat,"PATH") X(test,"-e|-f|-d PATH, or STRING = STRING") X(cmp,"FILE FILE") \
 X(touch,"FILE") X(cp,"SOURCE DEST") X(mv,"SOURCE DEST") X(rm,"FILE") \
 X(mkdir,"DIR") X(rmdir,"DIR") X(tee,"[-a] FILE") X(true,"") X(false,"") \
 X(sed,"[-i] s/OLD/NEW/[g] [FILE] (literal)") X(awk,"'{print $N}', '{print NF}' or '{print NR}' [FILE]") X(diff,"[-u] OLD NEW") \
 X(rg,"[-il] TEXT [PATH] (recursive literal search)") X(sha256sum,"[FILE]") X(xargs,"[-n N] [-d CHAR] COMMAND [ARG ...]") \
 X(which,"COMMAND") X(commands,"[COMMAND]")
#define WORK_WRITABLE 2
#define WORK_DIRECTORY 3
enum shell_change { SH_NONE, SH_WRITE, SH_MOVE, SH_REMOVE, SH_MKDIR, SH_RMDIR };
