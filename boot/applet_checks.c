/* Read-only cases run against the freshly linked candidate, before disk commit.
   Each row is COMMAND|OUTPUT. The final separator permits guest pipelines. */
static int check_applet_suite(const char *suite,const char *tools){
    if(!*suite)return 1;
    char rows[1536];strcpy(rows,suite);
    unsigned cases=0;
    for(char *line=rows;*line;){
        char *next=strchr(line,'\n');if(next)*next++=0;
        char *expected=strrchr(line,'|');
        if(!expected||expected==line){diagnostic(NULL,"invalid applet test row");return 0;}
        *expected++=0;
        int tagged=line[0]=='@',want_status=0;
        if(tagged){
            if(line[1]<'0'||line[1]>'2'||line[2]!=' '||!line[3]){diagnostic(NULL,"invalid applet status prefix");return 0;}
            want_status=line[1]-'0';line+=3;
        }
        const char *text=NULL;unsigned size=0;int cwd;
        int err=tool_run(line,machine.source,tools,machine.cwd,&text,&size,&cwd);
        struct tool_output *out=(void *)(USER_BASE+0x180000);
        unsigned want=strlen(expected);
        int status=err==-4?out->status:err;
        int newline=want||!tagged;
        if((err && err!=-4)||status!=want_status||out->change||size!=want+newline||
           (want && memcmp(text,expected,want))||(newline && text[want]!='\n')){
            snprintf(machine.diagnostic,sizeof machine.diagnostic,"failed %.128s, expected %.128s, got %.160s (status %d)",
                     line,expected,text?text:"",status);
            puts(machine.diagnostic);return 0;
        }
        ++cases;if(!next)break;line=next;
    }
    printf("applet tests passed: %u\n",cases);
    return cases>0;
}
