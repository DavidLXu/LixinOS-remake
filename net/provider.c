#include "abi.h"
#include "transport.h"
#include "bearssl.h"
#include "anchors.h"
#include "cJSON.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char credential[256], origin[256];
static br_ssl_client_context tls;
static br_x509_minimal_context verifier;
static br_sslio_context channel;
static unsigned char tls_buffer[BR_SSL_BUFSIZE_BIDI];
static int secure;
void set_key(const char *url, const char *key) {
    memset(credential,0,sizeof credential);
    snprintf(credential,sizeof credential,"%s",key);
    snprintf(origin,sizeof origin,"%s",url);
}
static int write_all(const char *s, size_t n) {
    while (n) {
        int k = secure ? br_sslio_write(&channel,s,n) : transport_write(NULL,(const unsigned char *)s,n);
        if (k <= 0) return 0;
        n -= k; s += k;
    }
    return 1;
}
struct reader { unsigned char buf[2048]; int pos, len; };
static int octet(struct reader *r) {
    if (r->pos == r->len) {
        int n = secure ? br_sslio_read(&channel,r->buf,sizeof r->buf) :
                         transport_read(NULL,r->buf,sizeof r->buf);
        if (n <= 0) return -1;
        r->pos = 0; r->len = n;
    }
    return r->buf[r->pos++];
}
static int read_line(struct reader *r, char *line, size_t cap) {
    size_t n = 0;
    int c;
    while ((c=octet(r)) >= 0 && c != '\n') {
        if (n+1 >= cap) return 0;
        line[n++] = c;
    }
    if (c < 0 || !n || line[n-1] != '\r') return 0;
    line[n-1] = 0; return 1;
}
static char reply[65536];
static char object_only[256];
static int body(struct reader *r, long size, int chunked) {
    size_t n = 0;
    char line[1024];
    for (;;) {
        if (chunked) {
            if (!read_line(r,line,sizeof line)) return 0;
            char *end;
            size = strtol(line,&end,16);
            if (end == line || (*end && *end != ';') || size < 0) return 0;
            if (!size) break;
        }
        if (size < 0) {
            int c;
            while ((c=octet(r)) >= 0) {
                if (n+1 == sizeof reply) return 0;
                reply[n++] = c;
            }
            break;
        }
        if ((unsigned long)size >= sizeof reply-n) return 0;
        for (long i=0;i<size;i++) {
            int c = octet(r); if (c < 0) return 0;
            reply[n++] = c;
        }
        if (!chunked) break;
        if (octet(r) != '\r' || octet(r) != '\n') return 0;
    }
    reply[n] = 0; return 1;
}
static int request(const char *url, const char *json, char *error, size_t cap) {
    secure = !strncmp(url,"https://",8);
    if (!secure && strncmp(url,"http://",7)) goto invalid;
    const char *authority = url + (secure ? 8 : 7), *path = strchr(authority,'/');
    if (!path || path == authority || path-authority >= 128) goto invalid;
    for (const char *p=url; *p; ++p) if ((unsigned char)*p <= 32 || *p == 127) goto invalid;
    char host[128], host_header[128];
    snprintf(host,sizeof host,"%.*s",(int)(path-authority),authority);
    strcpy(host_header,host);
    unsigned port = secure ? 443 : 80;
    char *colon = strchr(host,':');
    if (colon) {
        char *end;
        *colon++ = 0; unsigned long p = strtoul(colon,&end,10);
        if (*end || !p || p>65535) goto invalid;
        port = p;
    }
    if (!transport_connect(host,port,error,cap)) return 0;
    if (secure) {
        unsigned char entropy[32];
        if (!guest_entropy(entropy,sizeof entropy)) { snprintf(error,cap,"guest RNG failed"); goto fail; }
        br_ssl_client_init_full(&tls,&verifier,TAs,TAs_NUM);
        u32 seconds = *(volatile u32 *)0x09010000;
        br_x509_minimal_set_time(&verifier,719528+seconds/86400,seconds%86400);
        br_ssl_engine_inject_entropy(&tls.eng,entropy,sizeof entropy);
        br_ssl_engine_set_buffer(&tls.eng,tls_buffer,sizeof tls_buffer,1);
        if (!br_ssl_client_reset(&tls,host,0)) goto tls_error;
        br_sslio_init(&channel,&tls.eng,transport_read,NULL,transport_write,NULL);
    }
    char header[1024];
    int auth = secure && credential[0] && !strcmp(origin,url);
    int hn = snprintf(header,sizeof header,"POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nConnection: close\r\n%s%s%sContent-Length: %lu\r\n\r\n",
        path,host_header,auth?"Authorization: Bearer ":"",auth?credential:"",auth?"\r\n":"",(unsigned long)strlen(json));
    if (hn < 0 || hn >= (int)sizeof header || !write_all(header,hn) || !write_all(json,strlen(json)) ||
        (secure && br_sslio_flush(&channel) < 0)) goto tls_error;
    struct reader r = {0};
    char line[2048]; int status;
    if (!read_line(&r,line,sizeof line) || sscanf(line,"HTTP/%*s %d",&status) != 1) goto tls_error;
    if (status != 200) { snprintf(error,cap,"provider HTTP %d",status); goto fail; }
    int chunked = 0; long size = -1;
    do {
        if (!read_line(&r,line,sizeof line)) goto tls_error;
        if (!strncasecmp(line,"Content-Length:",15)) {
            char *end; size = strtol(line+15,&end,10);
            if (*end || size < 0) goto tls_error;
        }
        if (!strcasecmp(line,"Transfer-Encoding: chunked")) chunked = 1;
    } while (*line);
    if (!body(&r,size,chunked)) goto tls_error;
    transport_close(); return 1;
invalid:
    snprintf(error,cap,"invalid model endpoint URL"); return 0;
tls_error:
    snprintf(error,cap,"incomplete HTTP response or transport failure (TLS %d)",
             secure ? br_ssl_engine_last_error(&tls.eng) : 0);
fail:
    transport_close(); return 0;
}
int probe(const char *url,const char *model,char *message,unsigned cap) {
    cJSON *req=cJSON_CreateObject(), *list=cJSON_AddArrayToObject(req,"messages"), *turn=cJSON_CreateObject();
    cJSON_AddStringToObject(req,"model",model); cJSON_AddNumberToObject(req,"max_tokens",8);
    cJSON_AddItemToArray(list,turn); cJSON_AddStringToObject(turn,"role","user"); cJSON_AddStringToObject(turn,"content","Reply OK.");
    char *wire=cJSON_PrintUnformatted(req); cJSON_Delete(req);
    if (!wire) return 0;
    int ok=request(url,wire,message,cap); free(wire);
    if (ok) snprintf(message,cap,"provider HTTP 200");
    return ok;
}
/* Some model encoders decode the C token '\\n' twice in JSON tool arguments.
   The three-byte quote/LF/quote sequence is illegal C. Recover that exact token. */
static int c_char_literals(char *s, size_t cap) {
    char *p=s;
    while ((p=strstr(p,"'\n'"))!=NULL) {
        if (strlen(s)+1>=cap) return 0;
        memmove(p+3,p+2,strlen(p+2)+1);
        p[1]='\\'; p[2]='n'; p+=4;
    }
    return 1;
}
static int field(cJSON *obj,const char *name,char *dst,size_t cap) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj,name);
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= cap) return 0;
    strcpy(dst,v->valuestring); return 1;
}
int propose(const char *url,const char *model,const char *goal,const char *source,
            const char *feedback,const char *command,const char *expected,int query_only,struct proposal *out) {
    memset(out,0,sizeof *out);
    static const char instructions[] =
        "You maintain a bare-metal self-building C kernel. You receive an exact C source excerpt. "
        "If another function is needed, return only {\"read\":\"function_name\"}; use kernel.c for the whole file. "
        "For adding a new read-only command with textual output, prefer the add-query tool: "
        "{\"query\":\"species\",\"expression\":\"\\\"oak\\\"\",\"expected\":\"oak\"}. "
        "The expression is C code of type const char *; the kernel prints it followed by a newline. "
        "For a general edit, return this exact JSON shape: {\"before\":\"old C text\",\"after\":\"new C text\",\"command\":\"shell query\",\"expected\":\"output\"}. The fields are "
        "before, after, command, expected. before is a UNIQUE exact substring of the supplied current C source, "
        "after is its replacement (may be multiline). Change only code needed for the task. "
        "For a general edit use the shortest unique anchor: replacing just a literal is allowed. "
        "To replace the whole exact excerpt, use @excerpt as before. query_command returns an integer "
        "indicating that the command was handled; call say() to print text and put(10) for a newline. "
        "command is a read-only shell command and expected is "
        "its exact complete output without the final newline. Preserve any operator-specified acceptance test. "
        "Return JSON directly with no markdown. Consult previous build/test diagnostics to repair a failed patch.";
    cJSON *req=cJSON_CreateObject(), *info=cJSON_CreateObject();
    cJSON_AddStringToObject(info,"task",goal);
    cJSON_AddStringToObject(info,"diagnostics",feedback);
    cJSON_AddStringToObject(info,"required_command",command);
    cJSON_AddStringToObject(info,"required_output",expected);
    char *input=cJSON_PrintUnformatted(info); cJSON_Delete(info);
    if (!input) { cJSON_Delete(req); return 0; }
    cJSON_AddStringToObject(req,"model",model);
    cJSON *messages=cJSON_AddArrayToObject(req,"messages");
    cJSON *system=cJSON_CreateObject(), *user=cJSON_CreateObject();
    cJSON *code=cJSON_CreateObject();
    cJSON_AddItemToArray(messages,system); cJSON_AddItemToArray(messages,code); cJSON_AddItemToArray(messages,user);
    cJSON_AddStringToObject(code,"role","user"); cJSON_AddStringToObject(code,"content",source);
    cJSON_AddStringToObject(system,"role","system"); cJSON_AddStringToObject(system,"content",query_only ?
        "Generate a C expression of type const char * for the requested bare-metal shell query. "
        "The runtime prints the result and a newline. Return only a JSON object containing expression. "
        "Example for a fixed output of red: {\"expression\":\"\\\"red\\\"\"}. "
        "For computed output, write a C expression. You can use m->slot, m->note, and BUILD_NUMBER. "
        "The task defines the requirement. Fix compiler or behavioral errors reported in diagnostics." : instructions);
    cJSON_AddStringToObject(user,"role","user"); cJSON_AddStringToObject(user,"content",input); free(input);
    cJSON *format=cJSON_AddObjectToObject(req,"response_format");
    int schema = strcmp(object_only,url) != 0;
    cJSON_AddStringToObject(format,"type",schema ? "json_schema" : "json_object");
    if (schema) {
        cJSON *def=cJSON_AddObjectToObject(format,"json_schema");
        cJSON_AddStringToObject(def,"name","kernel_tool");
        cJSON_AddItemToObject(def,"schema",cJSON_Parse(query_only ?
            "{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\"}},"
            "\"required\":[\"expression\"],\"additionalProperties\":false}" :
            "{\"oneOf\":[{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},"
            "\"expression\":{\"type\":\"string\"},\"expected\":{\"type\":\"string\"}},"
            "\"required\":[\"query\",\"expression\",\"expected\"],\"additionalProperties\":false},"
            "{\"type\":\"object\",\"properties\":{\"before\":{\"type\":\"string\"},"
            "\"after\":{\"type\":\"string\"},\"command\":{\"type\":\"string\"},\"expected\":{\"type\":\"string\"}},"
            "\"required\":[\"before\",\"after\",\"command\",\"expected\"],\"additionalProperties\":false},"
            "{\"type\":\"object\",\"properties\":{\"read\":{\"type\":\"string\"}},\"required\":[\"read\"],\"additionalProperties\":false}]}"));
    }
    cJSON_AddNumberToObject(req,"max_tokens",2048);
    char *wire=cJSON_PrintUnformatted(req);
    int ok=wire && request(url,wire,out->message,sizeof out->message); free(wire);
    if (!ok && schema && (!strcmp(out->message,"provider HTTP 400") || !strcmp(out->message,"provider HTTP 422"))) {
        cJSON_DeleteItemFromObject(req,"response_format");
        format=cJSON_AddObjectToObject(req,"response_format");
        cJSON_AddStringToObject(format,"type","json_object");
        wire=cJSON_PrintUnformatted(req);
        ok=wire && request(url,wire,out->message,sizeof out->message); free(wire);
        if (ok) snprintf(object_only,sizeof object_only,"%s",url);
    }
    cJSON_Delete(req);
    if (!ok) return 0;
    cJSON *envelope=cJSON_Parse(reply);
    cJSON *choice=cJSON_GetArrayItem(cJSON_GetObjectItem(envelope,"choices"),0);
    cJSON *content=cJSON_GetObjectItem(cJSON_GetObjectItem(choice,"message"),"content");
    char *s=cJSON_GetStringValue(content);
    if (s && !strncmp(s,"```",3)) {
        s=strchr(s,'\n'); if(s) { ++s; char *end=strstr(s,"```"); if(end) *end=0; }
    }
    cJSON *patch=s?cJSON_ParseWithOpts(s,NULL,1):NULL;
    if (query_only && field(patch,"expression",out->after,sizeof out->after) && out->after[0]) {
        snprintf(out->command,sizeof out->command,"%s",command);
        cJSON_Delete(patch); cJSON_Delete(envelope); return 3;
    }
    if (field(patch,"query",out->command,sizeof out->command) && out->command[0] &&
        field(patch,"expression",out->after,sizeof out->after) && out->after[0] &&
        (command[0] || field(patch,"expected",out->expected,sizeof out->expected))) {
        cJSON_Delete(patch); cJSON_Delete(envelope); return 3;
    }
    if (field(patch,"read",out->read,sizeof out->read) && out->read[0]) {
        cJSON_Delete(patch); cJSON_Delete(envelope); return 2;
    }
    ok=field(patch,"before",out->before,sizeof out->before) && out->before[0] &&
       field(patch,"after",out->after,sizeof out->after);
    if (ok && !strcmp(out->before,"@excerpt")) {
        if (strlen(source)>=sizeof out->before) ok=0; else strcpy(out->before,source);
    }
    if (ok) ok=c_char_literals(out->before,sizeof out->before) && c_char_literals(out->after,sizeof out->after);
    if (!command[0]) {
        ok=ok && field(patch,"command",out->command,sizeof out->command) && out->command[0] &&
           field(patch,"expected",out->expected,sizeof out->expected);
    }
    if (!ok) snprintf(out->message,sizeof out->message,
        "Invalid proposal. Need unique before/after C substrings or a read field. Previous response: %.360s",s?s:"(empty)");
    cJSON_Delete(patch); cJSON_Delete(envelope);

    return ok ? 1 : -1;
}

/* Each call starts a new exchange. Package discovery sends the current catalog. */
int package_proposal(const char *url,const char *model,const char *goal,const char *context,
                     int mode,struct proposal *out) {
    memset(out,0,sizeof *out);
    cJSON *req=cJSON_CreateObject();
    cJSON_AddStringToObject(req,"model",model);
    cJSON_AddStringToObject(cJSON_AddObjectToObject(req,"response_format"),"type","json_object");
    cJSON_AddNumberToObject(req,"max_tokens",mode==1?8192:3000);
    if (!strcmp(url,"https://api.deepseek.com/chat/completions") && !strcmp(model,"deepseek-flash"))
        cJSON_AddStringToObject(cJSON_AddObjectToObject(req,"thinking"),"type","disabled");
    cJSON *messages=cJSON_AddArrayToObject(req,"messages");
    const char *instructions=mode==1?
        "Write one freestanding foreground C program. Return JSON fields source (complete C) and usage "
        "(one line describing arguments and purpose). Include app.h. Implement int main(int argc,char **argv); "
        "return 0 on success, nonzero on error. Only the listed SDK functions exist. No libc or stdio. "
        "Tests specify arguments|exact stdout, with one trailing newline implicit. Return numeric output alone when specified." :
        mode==3?
        "Choose a verified workflow by matching the requested outcome against the supplied FLOW menu. "
        "Return a single JSON object with the field flow. Copy its exact name from the matching menu line, "
        "or return {\"flow\":\"\"} when none matches. A workflow can acquire its own missing tools. "
        "Choose by meaning and output format across languages." :
        mode==2?
        "Return exactly one JSON object. Select by the requested FINAL output. "
        "Use a command only when its direct stdout fully satisfies the task. For per-file output a final absolute glob "
        "runs the command on each matching file. Return {\"command\":\"command with args\"}. "
        "For a missing command return {\"new_tool\":\"conventional lowercase command name\"}." :
        "Choose an installed command from the guest catalog to fulfill the task. Return JSON with a single string "
        "field command. For each file in a directory, put an absolute glob like /src/user/*.c as the last argument. "
        "The guest expands it and invokes the command separately for each file, labeling each result. No pipelines.";
    const char *roles[]={"system","user","user"}, *items[]={instructions,context,goal};
    for (unsigned i=0;i<3;++i) {
        cJSON *turn=cJSON_CreateObject(); cJSON_AddItemToArray(messages,turn);
        cJSON_AddStringToObject(turn,"role",roles[i]); cJSON_AddStringToObject(turn,"content",items[i]);
    }
    char *wire=cJSON_PrintUnformatted(req); cJSON_Delete(req);
    int ok=wire && request(url,wire,out->message,sizeof out->message);
    free(wire); if (!ok) return 0;
    cJSON *envelope=cJSON_Parse(reply);
    cJSON *choice=cJSON_GetArrayItem(cJSON_GetObjectItem(envelope,"choices"),0);
    char *content=cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(choice,"message"),"content"));
    cJSON *answer=content?cJSON_ParseWithOpts(content,NULL,1):NULL;
    ok=mode==1 ? field(answer,"source",out->after,sizeof out->after) && out->after[0] &&
                field(answer,"usage",out->expected,sizeof out->expected) && out->expected[0] &&
                c_char_literals(out->after,sizeof out->after) :
                field(answer,"command",out->command,sizeof out->command) && out->command[0];
    if (mode==3) ok=field(answer,"flow",out->read,sizeof out->read) ? (out->read[0]?3:4) : 0;
    if (!ok && mode==2 && field(answer,"new_tool",out->read,sizeof out->read) && out->read[0]) ok=2;
    if (!ok) {
        const char *reason=cJSON_GetStringValue(cJSON_GetObjectItem(choice,"finish_reason"));
        snprintf(out->message,sizeof out->message,"invalid package response (finish: %.40s)",reason?reason:"unknown");
    }
    cJSON_Delete(answer); cJSON_Delete(envelope);
    return ok;
}
