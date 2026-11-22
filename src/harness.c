/* Guest-owned job journal. Each bank uses unused sectors after the slot's source. */
enum { IDLE, WORK, TRIAL, DONE, STOPPED };
struct job {
    u32 phase, attempts, baseline, candidate, target;
    u64 epoch;
    char url[256], model[64], goal[2048], command[128], expected[256];
    char feedback[1024], log[4096], draft[SOURCE_CAP];
    unsigned test_count;
    struct { char command[128], output[256]; } tests[12];
    int user_test, next_user_test, query_only;
    char focus[128];
    unsigned edit_offset;
    char last_before[6144], last_after[8192];
};
typedef char journal_must_fit_slot[(sizeof(struct job) <= (1024-651)*512) ? 1 : -1];
struct stamp { u32 magic, size, checksum, serial, guard; };
static struct job job;
static unsigned serial;
static struct proposal patch;
static int text_set(char *dst, unsigned cap, const char *s) {
    unsigned n = length(s);
    if (n >= cap) return 0;
    copy((u8 *)dst,(const u8 *)s,n+1); return 1;
}
static void feedback(const char *s) {
    text_set(job.feedback,sizeof job.feedback,s);
    unsigned used = length(job.log), n = length(s);
    if (used+n+2 >= sizeof job.log) { used = 0; job.log[0] = 0; }
    copy((u8 *)job.log+used,(const u8 *)s,n); job.log[used+n] = '\n'; job.log[used+n+1] = 0;
}
static unsigned journal_base(unsigned sequence) { return (sequence%2)*1024 + 650; }
static int stamp_valid(unsigned bank, struct stamp *h) {
    return blocks(0,bank*1024+650,(u8 *)h,sizeof *h) && h->magic == 0x4a423031 &&
        h->size == sizeof job && h->serial % 2 == bank && h->guard == digest(h,16);
}
static int recover(unsigned bank, struct stamp *h) {
    if (!stamp_valid(bank,h) || !blocks(0,bank*1024+651,(u8 *)&job,sizeof job) ||
        h->checksum != digest(&job,sizeof job)) return 0;
    serial = h->serial; return 1;
}
static int save_job(void) {
    struct stamp h;
    h.magic = 0x4a423031; h.size = sizeof job; h.serial = serial+1;
    h.checksum = digest(&job,sizeof job); h.guard = digest(&h,16);
    unsigned block = journal_base(h.serial);
    if (!blocks(1,block+1,(u8 *)&job,sizeof job) || !io(4,0) ||
        !blocks(1,block,(u8 *)&h,sizeof h) || !io(4,0)) {
        say("journal write failed\n"); return 0;
    }
    serial = h.serial; return 1;
}
static int query_patch(void) {
    for (const char *p=patch.command; *p; ++p) {
        if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') ||
              (*p>='0' && *p<='9') || *p=='-' || *p=='_')) return 0;
    }
    static const char anchor[]="\nstatic int query_command(const char *cmd) {\n";
    const char *pieces[]={anchor,"    if (same(cmd,\"",patch.command,"\")) { say(",
                          patch.after,"); put('\\n'); return 1; }\n"};
    static char code[sizeof patch.after];
    unsigned used=0;
    for (unsigned i=0;i<sizeof pieces/sizeof pieces[0];++i) {
        unsigned n=length(pieces[i]);
        if (used+n>=sizeof code) return 0;
        copy((u8 *)code+used,(const u8 *)pieces[i],n); used+=n;
    }
    code[used]=0;
    const char *before=job.query_only && job.last_after[0] ? job.last_after : anchor;
    if (!text_set(patch.before,sizeof patch.before,before)) return 0;
    return text_set(patch.after,sizeof patch.after,code);
}
static int apply_patch(void) {
    unsigned n=length(job.draft), old=length(patch.before), fresh=length(patch.after);
    if (!old || old>n || n-old+fresh>=sizeof job.draft) return 0;
    unsigned at=0, matches=0;
    for (unsigned i=0; i+old<=n; ++i) {
        if (prefix(job.draft+i,patch.before)) { at=i; if (++matches>1) return 0; }
    }
    if (matches!=1 || same(patch.before,patch.after)) return 0;
    if (fresh>old) {
        for (unsigned i=n+1; i>at+old; --i) job.draft[i-1+fresh-old]=job.draft[i-1];
    } else {
        for (unsigned i=at+old; i<=n; ++i) job.draft[i-old+fresh]=job.draft[i];
    }
    job.edit_offset=at;
    copy((u8 *)job.draft+at,(const u8 *)patch.after,fresh);
    text_set(job.last_before,sizeof job.last_before,patch.before);
    text_set(job.last_after,sizeof job.last_after,patch.after);
    return 1;
}
static char excerpt[8192];
static const char *read_context(void) {
    if (same(job.focus,"kernel.c")) return job.draft;
    unsigned n=length(job.focus);
    for (const char *line=job.draft; *line;) {
        const char *e=line;
        while (*e && *e!='\n') ++e;
        if (*line!=' ' && *line!='\t' && *line!='/' && *line!='#') {
            for (const char *p=line; p<e; ++p) {
                if (!prefix(p,job.focus) || p[n]!='(' || (p>line && p[-1]!=' ' && p[-1]!='*')) continue;
                const char *end=e;
                if (end>line && end[-1]==';') break;
                while (*end && !prefix(end,"\n}")) ++end;
                unsigned bytes=end-line+(*end?2:0);
                if (bytes >= sizeof excerpt) bytes=sizeof excerpt-1;
                copy((u8 *)excerpt,(const u8 *)line,bytes); excerpt[bytes]=0;
                return excerpt;
            }
        }
        line=*e?e+1:e;
    }
    if (job.last_after[0] && job.edit_offset<length(job.draft)) {
        unsigned at=job.edit_offset;
        while (at && job.draft[at-1]!='\n') --at;
        unsigned n=length(job.draft+at);
        if (n>=sizeof excerpt) n=sizeof excerpt-1;
        copy((u8 *)excerpt,(u8 *)job.draft+at,n); excerpt[n]=0;
        return excerpt;
    }
    return "Symbol not found. Request kernel.c to inspect the full source.";
}
static void agent_loop(void);
static int check_one(const char *command,const char *expected,int record_error) {
    observed_len = overflow = 0; capturing = 1;
    int handled = query_command(command);
    capturing = 0;
    if (observed_len && observed[observed_len-1]=='\n') --observed_len;
    observed[observed_len]=0;
    int ok = handled && !overflow && same(observed,expected);
    say("acceptance ["); say(command); say("] => "); say(observed); put('\n');
    if (!ok && record_error) {
        feedback("failed query and expected output (in order):");
        feedback(command); feedback(expected);
    }
    return ok;
}
static int check_behavior(int record_error) {
    if (!check_one(job.command,job.expected,record_error)) return 0;
    for (unsigned i=0;i<job.test_count;++i) {
        if (!same(job.tests[i].command,job.command) &&
            !check_one(job.tests[i].command,job.tests[i].output,record_error)) return 0;
    }
    return 1;
}
static int remember_test(void) {
    unsigned i=0;
    while (i<job.test_count && !same(job.tests[i].command,job.command)) ++i;
    if (i==12) return 0;
    if (i==job.test_count) ++job.test_count;
    text_set(job.tests[i].command,sizeof job.tests[i].command,job.command);
    text_set(job.tests[i].output,sizeof job.tests[i].output,job.expected);
    return 1;
}
static void finish_trial(void) {
    struct record r;
    if (check_behavior(1)) {
        r.epoch=m->epoch; r.magic=0x31584b4c; r.abi=ABI_VERSION;
        r.elf_len=m->image_len; r.src_len=m->source_len;
        r.elf_hash=digest(m->image,r.elf_len); r.src_hash=digest(m->source,r.src_len);
        if (remember_test() && r.src_hash==job.candidate && read_payload(m->slot,&r) && publish_header(m->slot,&r)) {
            m->end_trial(); m->trial=0; job.phase=DONE; feedback("PASS: candidate accepted and committed");
            if (save_job()) say("agent: committed\n");
            return;
        }
        feedback("candidate disk verification or commit failed");
    } else {
        feedback("FAIL: candidate output differs from the required output");
        feedback(observed[0] ? observed : "empty output or missing command");
    }
    job.phase=WORK;
    if (!save_job()) { say("restart to recover previous kernel\n"); return; }
    int prior=1-m->slot;
    if (load_record(prior,&r)) {
        m->trial=2; ++m->switches; select_record(prior,&r);
    }
    say("previous slot unavailable\n");
}
static void agent_boot(void) {
    struct stamp a,b;
    int va=stamp_valid(0,&a), vb=stamp_valid(1,&b);
    unsigned first=vb && (!va || b.serial>a.serial);
    if (!recover(first,&a) && !recover(1-first,&a)) {
        u8 *p=(u8 *)&job; for (unsigned i=0;i<sizeof job;++i) p[i]=0;
        text_set(job.url,sizeof job.url,"https://api.deepseek.com/chat/completions");
        text_set(job.model,sizeof job.model,"deepseek-flash");
    }
    if (job.phase==TRIAL) {
        if (m->trial==1 && m->slot==(int)job.target && m->epoch==job.epoch) {
            finish_trial(); return;
        }
        if (m->slot==(int)job.target && m->epoch==job.epoch &&
            digest(m->source,m->source_len)==job.candidate) {
            remember_test(); job.phase=DONE; feedback("recovered committed candidate");
        } else {
            job.phase=WORK; feedback("trial interrupted; previous kernel retained; resume to repair");
        }
        save_job();
    }
    if (m->trial==2) { m->trial=0; if (job.phase==WORK) agent_loop(); }
}
static void agent_loop(void) {
    struct record active, candidate;
    if (m->slot<0 || !load_record(m->slot,&active) || active.src_hash!=job.baseline ||
        digest(m->source,m->source_len)!=job.baseline) {
        say("task base changed; start a new task\n"); return;
    }
    while (job.attempts < 6) {
        say("model turn "); number(job.attempts+1); put('\n');
        int result=m->propose(job.url,job.model,job.goal,read_context(),job.log,
                              job.command,job.expected,job.query_only,&patch);
        if (result==0) {
            feedback(patch.message[0]?patch.message:"provider unavailable");
            say(job.feedback); say("; use agent resume\n"); save_job(); return;
        }
        ++job.attempts;
        if (result==2) {
            text_set(job.focus,sizeof job.focus,patch.read);
            say("reading "); say(job.focus); put('\n');
            if (!save_job()) return;
            continue;
        }
        if (result<0) feedback(patch.message);
        else if ((result==3 && !query_patch()) || !apply_patch()) {
            feedback("edit rejected: use a short unique exact anchor, or @excerpt to replace the current function");
            if (length(patch.before)<400) feedback(patch.before);
        }
        else {
            if (!job.user_test && !job.command[0]) {
                text_set(job.command,sizeof job.command,patch.command);
                text_set(job.expected,sizeof job.expected,patch.expected);
            }
            m->source_len=length(job.draft);
            text_set(m->source,sizeof m->source,job.draft);
            int compiled=m->compile(m,BUILD_NUMBER+1);
            if (compiled) {
                int target=1-m->slot;
                job.target=target; job.candidate=digest(m->source,m->source_len);
                job.epoch=m->epoch+1;
                /* Unpublish the inactive header before changing its bytes. Cold boot
                   must select the confirmed kernel until the behavioral test passes. */
                struct record blank = {0};
                if (blocks(1,base(target),(u8 *)&blank,sizeof blank) && io(4,0) && stage(target,&candidate)) {
                    job.phase=TRIAL; feedback("TCC built candidate; starting trial");
                    if (save_job()) {
                        m->trial=1; ++m->switches; select_record(target,&candidate);
                    }
                }
                feedback("unable to stage trial"); job.phase=WORK;
            } else {
                feedback("TCC compilation failed");
                feedback(m->diagnostic[0]?m->diagnostic:"invalid candidate ELF");
            }
            /* A failed edit lives only in the journal. Restore the working shell. */
            if (!load_record(m->slot,&active)) { say("base read failed\n"); return; }
            copy((u8 *)m->source,(u8 *)saved_source,active.src_len);
            m->source[active.src_len]=0; m->source_len=active.src_len;
            copy(m->image,saved_image,active.elf_len); m->image_len=active.elf_len;
        }
        say(job.feedback); put('\n');
        if (!save_job()) return;
    }
    job.phase=STOPPED; feedback("six model turns exhausted"); save_job(); say("agent: stopped\n");
}
static void agent_command(char *cmd) {
    if (same(cmd,"agent probe")) {
        char result[256]={0}; m->probe(job.url,job.model,result,sizeof result); say(result); put('\n'); return;
    }
    if (same(cmd,"agent tests")) {
        for (unsigned i=0;i<job.test_count;++i) {
            say(job.tests[i].command); put('|'); say(job.tests[i].output); put('\n');
        }
        return;
    }
    if (same(cmd,"agent diff")) {
        say("--- before\n"); say(job.last_before); say("\n+++ after\n"); say(job.last_after); put('\n'); return;
    }
    if (same(cmd,"agent verify")) {
        if (!job.command[0]) say("no acceptance test\n");
        else say(check_behavior(0) ? "PASS\n" : "FAIL\n");
        return;
    }
    if (same(cmd,"agent status") || same(cmd,"agent log")) {
        say("model="); say(job.model); say(" endpoint="); say(job.url);
        say("\nphase="); const char *names[]={"idle","working","trial","done","stopped"};
        say(job.phase<5?names[job.phase]:"invalid"); say(" turns="); number(job.attempts);
        say("\ngoal="); say(job.goal); say("\ntest="); say(job.command); put('|'); say(job.expected);
        put('\n'); say(job.log); return;
    }
    if (same(cmd,"agent key")) {
        if (!prefix(job.url,"https://")) { say("key requires HTTPS\n"); return; }
        char secret[256]; say("API key (hidden): ");
        if (input_line(secret,sizeof secret,0)) m->set_key(job.url,secret);
        volatile char *wipe=secret; for (unsigned i=0;i<sizeof secret;++i) wipe[i]=0;
        say("key input finished\n"); return;
    }
    if (prefix(cmd,"agent model ")) {
        char *url=cmd+12, *name=url;
        while (*name && *name!=' ') ++name;
        if (!*name) { say("agent model URL NAME\n"); return; }
        *name++=0;
        if (!*name || length(name)>=sizeof job.model || length(url)>=sizeof job.url ||
            !(prefix(url,"https://") || prefix(url,"http://"))) { say("invalid endpoint/model\n"); return; }
        if (!same(job.url,url)) m->set_key(url,"");
        text_set(job.url,sizeof job.url,url); text_set(job.model,sizeof job.model,name);
        if (save_job()) say("model saved\n");
        return;
    }
    if (same(cmd,"agent cancel")) {
        job.phase=STOPPED; feedback("cancelled by operator"); save_job(); return;
    }
    if (same(cmd,"agent resume")) {
        if (job.phase==WORK) agent_loop(); else say("no resumable task\n");
        return;
    }
    if (job.phase==WORK || job.phase==TRIAL) { say("use agent resume or agent cancel\n"); return; }
    if (prefix(cmd,"agent untest ")) {
        unsigned i=0;
        while (i<job.test_count && !same(job.tests[i].command,cmd+13)) ++i;
        if (i==job.test_count) { say("unknown retained query\n"); return; }
        --job.test_count;
        if (i<job.test_count) job.tests[i]=job.tests[job.test_count];
        if (save_job()) say("test removed\n");
        return;
    }
    if (prefix(cmd,"agent test ")) {
        char *command=cmd+11, *out=command;
        if (same(command,"auto")) { job.next_user_test=0; job.command[0]=job.expected[0]=0; }
        else {
            while (*out && *out!='|') ++out;
            if (!*out || out==command) { say("agent test COMMAND|OUTPUT\n"); return; }
            *out++=0;
            if (length(command)>=sizeof job.command || length(out)>=sizeof job.expected) { say("test too long\n"); return; }
            text_set(job.command,sizeof job.command,command); text_set(job.expected,sizeof job.expected,out);
            job.next_user_test=1;
        }
        save_job(); return;
    }
    int query_only=prefix(cmd,"agent query ");
    if ((query_only || prefix(cmd,"agent run ")) && cmd[query_only?12:10]) {
        if (query_only && !job.next_user_test) { say("set agent test COMMAND|OUTPUT first\n"); return; }
        struct record r;
        if (m->slot<0 || !load_record(m->slot,&r) || digest(m->source,m->source_len)!=r.src_hash) {
            say("commit pending manual edits first\n"); return;
        }
        text_set(job.focus,sizeof job.focus,"query_command");
        job.phase=WORK; job.attempts=0; job.baseline=r.src_hash;
        job.log[0]=job.feedback[0]=job.last_before[0]=job.last_after[0]=0;
        job.query_only=query_only; job.user_test=job.next_user_test; job.next_user_test=0;
        if (!job.user_test) job.command[0]=job.expected[0]=0;
        text_set(job.goal,sizeof job.goal,cmd+(query_only?12:10)); text_set(job.draft,sizeof job.draft,m->source);
        if (save_job()) agent_loop();
        return;
    }
    say("agent model URL NAME | agent key | agent test COMMAND|OUTPUT | agent run GOAL | agent query GOAL | agent status | agent resume | agent cancel\n");
}
