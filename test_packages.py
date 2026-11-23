"""Cold-boot a new guest task with only the installed catalog and its new goal."""
import argparse
import contextlib
import hashlib
import http.server
import json
import os
from pathlib import Path
import pwd
import re
import struct
import subprocess
import tempfile
import threading

from test import ROOT, boot, expect

MAKE = ('agent make wc /src/validation/lines.tests Implement wc -l FILE. '
        'Count newline bytes, print only the decimal count and a newline. Read in chunks.')
TASK = ('agent task /src/validation/lines.tests Provide the conventional Unix command for counting '
        'newline bytes in a file. Accept -l FILE, read in chunks, print the decimal count and a newline.')
USE = 'agent task 统计 /src/user/ 下各 C 文件的行数'
SAMPLE = '''#include "app.h"
static void number(unsigned n) { if (n>=10) number(n/10); putchar('0'+n%10); }
int main(int argc,char **argv) {
    if (argc!=3) return 1;
    int fd=file_open(argv[2]), n;
    char block[32]; unsigned count=0;
    if (fd<0) return 1;
    while ((n=file_read(fd,block,sizeof block))>0)
        for (int i=0;i<n;++i) if (block[i]==10) ++count;
    file_close(fd); number(count); putchar(10); return n<0;
}
'''


class Fixture(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        req = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        context, goal = [m['content'] for m in req['messages'][1:]]
        if goal == USE.removeprefix('agent task '):
            assert 'wc' in context and '#include' not in context and 'wc' not in goal
            result = {'command': 'wc -l /src/user/*.c'}
        elif context=='(empty)':
            result = {'new_tool':'wc'}
        else:
            source = SAMPLE if 'reject' not in goal else SAMPLE.replace('++count','count+=2')
            if 'variant' in goal:
                source=source.replace('char block[32]', 'char block[48]')
            result = {'source': source, 'usage': '-l FILE counts newline bytes'}
        body = json.dumps({'choices':[{'message':{'content':json.dumps(result)}}]}).encode()
        self.send_response(200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


@contextlib.contextmanager
def provider(live):
    if live:
        key = subprocess.check_output(['/usr/bin/security','find-generic-password','-a',pwd.getpwuid(os.getuid()).pw_name,
                                       '-s','LixinOS.DeepSeek','-w'],stderr=subprocess.DEVNULL).decode().strip()
        assert key
        yield None,key
        return
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture) as server:
        worker=threading.Thread(target=server.serve_forever,daemon=True)
        worker.start()
        try:
            yield f'http://10.0.2.2:{server.server_port}/chat/completions',None
        finally:
            server.shutdown()
            worker.join()


def disk_kernel(disk):
    with disk.open('rb') as f:
        parts=[]
        for slot in (0,1):
            f.seek(slot*1024*512)
            parts.append(f.read(650*512))
        return parts


def exercise(disk,url,key):
    expected = {f'/src/user/{p.name}':p.read_bytes().count(b'\n') for p in (ROOT/'user').glob('*.c')}
    with boot(disk,timeout=300) as c:
        expect(c,None,'1.4.0 build=0 slot=A')
        if url:
            c('agent model '+url+' fixture')
        else:
            c('agent key',secret=key)
        expect(c,'apps','installed commands:\nkernel> ')
        before=disk_kernel(disk)
        output=c(TASK)
        assert "missing capability: wc" in output
        match=re.search(r'installed wc tests=3 elf=([0-9a-f]{64})',output)
        assert match,output
        fingerprint=match.group(1)
        expect(c,'cat /work/wc.build','/src/user/crt.c\n/work/wc.c')
        expect(c,'ls /work','wc.c\nwc.build')
        expect(c,'apps info wc','elf='+fingerprint)
        expect(c,'apps resume','draft already installed')
        expect(c,'cd /work','kernel> ')
        expect(c,'cat wc.build','/src/user/crt.c\n/work/wc.c')
        expect(c,'wc -l wc.build','\n2\n')
        expect(c,'tcc /work/wc.build','application ELF ready')
        expect(c,'run -l wc.build','\n2\n')
    with boot(disk,timeout=300) as c:
        expect(c,None,'1.4.0 build=0 slot=A hot=0')
        if key:
            c('agent key',secret=key)
        expect(c,'apps stats','application_builds=0 source_writes=0 executions=0')
        result=c(USE)
        assert 'discovered:\nwc ' in result and f'invoke wc elf={fingerprint}' in result
        counts=dict((path,int(n)) for path,n in re.findall(r'^(/src/user/\S+\.c) (\d+)$',result,re.M))
        assert counts==expected,(counts,expected)
        expect(c,'apps stats',f'application_builds=0 source_writes=0 executions={len(expected)}')
    assert disk_kernel(disk)==before
    data=disk.read_bytes()
    start=(47105+64+480)*512
    size=struct.unpack_from('<I',data,start+8)[0]
    elf_start=start+512*(1+(32+256+64+64+128+1536+8192+511)//512)
    assert hashlib.sha256(data[elf_start:elf_start+size]).hexdigest()==fingerprint
    if key:
        assert key.encode() not in data
    else:
        with boot(disk,timeout=90) as c:
            c()
            expect(c,MAKE+' reject','test failed:')
            expect(c,'wc -l /src/validation/terminated.txt','\n2\n')
        with boot(disk) as c:
            c()
            expect(c,'apps resume','test failed:')
            expect(c,'wc -l /src/validation/terminated.txt','\n2\n')
            expect(c,MAKE+' variant','installed wc tests=3')
        # Tear the most recent commit sector on the disposable disk.
        with disk.open('r+b') as f:
            f.seek((47105+64)*512)
            f.write(b'BAD!')
        with boot(disk) as c:
            c()
            expect(c,'apps info wc','elf='+fingerprint)
            expect(c,'wc -l /src/validation/terminated.txt','\n2\n')
            expect(c,'apps resume','installed wc tests=3')
        original=os.environ.get('LIXINOS_SOURCE_VOLUME')
        volume=bytearray(Path(original or ROOT/'release/sources.img').read_bytes())
        sdk=(ROOT/'user/app.h').read_bytes()
        offset=volume.index(sdk)+sdk.index(b'int file_read')+3
        volume[offset]=9   # Change SDK whitespace on a separate source volume.
        alternate=disk.with_name('changed-sdk.img')
        alternate.write_bytes(volume)
        try:
            os.environ['LIXINOS_SOURCE_VOLUME']=str(alternate)
            with boot(disk) as c:
                c()
                expect(c,'wc -l /src/validation/terminated.txt','source volume changed')
                expect(c,'apps resume','installed wc tests=3')
                expect(c,'wc -l /src/validation/terminated.txt','\n2\n')
        finally:
            if original is None:
                os.environ.pop('LIXINOS_SOURCE_VOLUME',None)
            else:
                os.environ['LIXINOS_SOURCE_VOLUME']=original
    print(f'PASS: cold catalog discovery, {len(expected)} independently counted C files, '
          f'zero application builds and source writes, unchanged kernel slots, ELF {fingerprint}')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--deepseek-keychain',action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp, provider(args.deepseek_keychain) as (url,key):
        exercise(Path(tmp)/'tasks.img',url,key)
