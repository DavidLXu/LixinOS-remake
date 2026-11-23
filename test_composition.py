"""Carry a real v1.4 wc across an upgrade, compose it, and falsify the guest evidence."""
import argparse
import contextlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from test import ROOT, boot, expect
from test_packages import Fixture, provider, disk_kernel, MAKE

GOAL = '找出 /src/user/ 里行数最多的 C 文件'
MAX_SOURCE = '''#include "app.h"
static char data[8192];
static int cmp(char *a,char *b) {
    while (*a && *a==*b) { ++a; ++b; }
    return *a-*b;
}
int main(int argc,char **argv) {
    if (argc!=2) return 1;
    int fd=file_open(argv[1]), n; unsigned used=0;
    if (fd<0) return 1;
    while (used<sizeof(data)-1 && (n=file_read(fd,data+used,sizeof(data)-1-used))>0) used+=n;
    file_close(fd); data[used]=0;
    char *p=data,*best=0,*value=0; unsigned largest=0;
    while (*p) {
        char *name=p; while (*p && *p!=' ') ++p;
        if (*p!=' ') return 1;
        *p++=0; char *digits=p; unsigned count=0;
        while (*p>='0' && *p<='9') count=count*10+*p++-'0';
        if (*p!='\\n') return 1;
        *p++=0;
        if (!best || count>largest || (count==largest && cmp(name,best)<0)) {
            best=name; value=digits; largest=count;
        }
    }
    if (!best) return 1;
    while (*best) putchar(*best++);
    putchar(' '); puts(value); return 0;
}
'''


class CompositionProvider(Fixture):
    def do_POST(self):
        data=self.rfile.read(int(self.headers['Content-Length']))
        request=json.loads(data)
        context,goal=[m['content'] for m in request['messages'][1:]]
        if goal==GOAL:
            assert 'FLOW longest:' in context
            response={'flow':'longest'}
        elif 'Name: max' in context:
            response={'source':MAX_SOURCE,'usage':'FILE selects the maximum path-count row'}
        else:
            import io
            self.rfile=io.BytesIO(data)
            return super().do_POST()
        body=json.dumps({'choices':[{'message':{'content':json.dumps(response)}}]}).encode()
        self.send_response(200)
        self.send_header('Content-Length',str(len(body)))
        self.end_headers()
        self.wfile.write(body)


@contextlib.contextmanager
def old_release(tmp,tag="v1.4.0"):
    saved={k:os.environ.get(k) for k in ('LIXINOS_BOOT_IMAGE','LIXINOS_SOURCE_VOLUME')}
    try:
        for env,name in zip(saved,('boot.elf','sources.img')):
            path=tmp/name
            path.write_bytes(subprocess.check_output(['git','show',f'{tag}:release/{name}'],cwd=ROOT))
            os.environ[env]=str(path)
        yield
    finally:
        for env,value in saved.items():
            if value is None: os.environ.pop(env,None)
            else: os.environ[env]=value


def command_hash(out,name):
    match=re.search(rf'installed {name} tests=3 elf=([0-9a-f]{{64}})',out)
    assert match, out
    return match.group(1)


def exercise(tmp,url,key):
    disk=tmp/'tasks.img'
    # Hold the pre-existing binary constant across offline and live trials.
    # The model under test sees this catalog after the upgrade and cold boot.
    setup=provider(False) if key else contextlib.nullcontext((url,None))
    with setup as (setup_url,_), old_release(tmp), boot(disk,timeout=420) as c:
        expect(c,None,'Kernel 1.4.0')
        c('agent model '+setup_url+' fixture')
        original=command_hash(c(MAKE),'wc')
        if key: c('agent model https://api.deepseek.com/chat/completions deepseek-flash')
    with boot(disk,launcher=[ROOT/'run.sh','--upgrade']) as c:
        expect(c,None,'Kernel 1.6.0')
        expect(c,'apps recheck wc','tests=3 elf='+original)
        expect(c,'apps stats','application_builds=0 source_writes=0')
    before=disk_kernel(disk)
    with boot(disk,timeout=420) as c:
        c()
        if key: c('agent key',secret=key)
        output=c('agent task '+GOAL)
        assert 'invoke wc elf='+original in output and 'installed wc' not in output
        if 'installed max' not in output:
            c('cat /work/max.c')
        reducer=command_hash(output,'max')
        assert 'invoke max elf='+reducer in output and 'complete counts and maximum verified' in output
        paths=sorted((ROOT/'user').glob('*.c'))
        counts=[(f'/src/user/{p.name}',p.read_bytes().count(b'\n')) for p in paths]
        rows=''.join(f'{p} {n}\n' for p,n in counts)
        winner,count=min(counts,key=lambda row:(-row[1],row[0]))
        best=f'{winner} {count}\n'
        assert 'verified result: '+best in output
        expect(c,'cat /work/counts.txt','\n'+rows)
        expect(c,'cat /work/answer.txt','\n'+best)
    with boot(disk) as c:
        c()
        expect(c,'result info','revision=1 producer='+original+'\nreducer='+reducer)
        expect(c,'result verify','complete counts and maximum verified')
        expect(c,'tcc /src/validation/omit.build','application ELF ready')
        expect(c,'run /work/counts.txt /src/user/hello.c > /work/scratch.txt','saved /work/scratch.txt')
        expected=''.join(f'{p} {n}\n' for p,n in counts if p!='/src/user/hello.c')
        expect(c,'cat /work/scratch.txt','\n'+expected+'kernel> ')
        expect(c,'cat /work/answer.txt','\n'+best)
        expect(c,'result verify /work/scratch.txt /work/answer.txt','missing input: /src/user/hello.c\nverification rejected')
        expect(c,'result verify /work/counts.txt /src/validation/max-middle.txt','incorrect maximum')
        expect(c,'result verify','complete counts and maximum verified')
    if not key:
        with boot(disk) as c:
            c()
            second=c('agent task '+GOAL)
            assert 'installed max' not in second and 'verified result: '+best in second
            expect(c,'apps stats','application_builds=1 source_writes=0')
            expect(c,'result info','revision=2 producer='+original)
        # Tear the latest result commit on this throwaway disk. Both files
        # must be restored from the same older receipt.
        with disk.open('r+b') as f:
            f.seek((47105+64+8*480)*512)
            f.write(b'BAD!')
        with boot(disk) as c:
            c()
            expect(c,'result info','revision=1 producer='+original)
            expect(c,'cat /work/counts.txt','\n'+rows)
            expect(c,'cat /work/answer.txt','\n'+best)
            expect(c,'result verify','complete counts and maximum verified')
    assert disk_kernel(disk)==before
    if key: assert key.encode() not in disk.read_bytes()
    print(f'PASS: v1.4 wc {original}, new max {reducer}, durable guest-verified answer {best.strip()}, omitted hello.c rejected')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--deepseek-keychain',action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp, provider(args.deepseek_keychain,CompositionProvider) as (url,key):
        exercise(Path(tmp),url,key)
