"""Install a second flow over a serial console with a frozen guest image."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import tempfile
from unittest.mock import patch

from test import ROOT, boot, expect
from test_composition import CompositionProvider, GOAL, command_hash, old_release
from test_packages import MAKE, disk_kernel, provider

TOTAL = '统计 /src/user/ 下所有 C 文件的总行数，只给一个合计数字'
BUNDLE = ROOT/'examples/total'
SUM_SOURCE = '''#include "app.h"
static void number(unsigned n) { if (n>=10) number(n/10); putchar(48+n%10); }
int main(int argc,char **argv) {
    if (argc!=2) return 1;
    int fd=file_open(argv[1]); if (fd<0) return 1;
    char c; unsigned sum=0,value=0; int digits=0,n;
    while ((n=file_read(fd,&c,1))==1) {
        if (c==' ') { digits=1; value=0; }
        else if (c==10) { if (!digits) return 1; sum+=value; digits=0; }
        else if (digits) { if (c<48 || c>57) return 1; value=value*10+c-48; }
    }
    file_close(fd); number(sum); putchar(10); return n<0 || digits;
}
'''


class FlowProvider(CompositionProvider):
    def do_POST(self):
        data=self.rfile.read(int(self.headers['Content-Length']))
        req=json.loads(data)
        context,goal=[m['content'] for m in req['messages'][1:]]
        if goal==GOAL and 'FLOW ' not in context:
            result={'flow':'longest','producer':'wc','reducer':'max'}  # v1.5 wire format
        elif goal==TOTAL:
            assert 'FLOW total:' in context and 'FLOW longest:' in context
            result={'flow':'total'}
        elif context.startswith('Name: sum\n'):
            result={'source':SUM_SOURCE,'usage':'FILE outputs the sum of the counts'}
        else:
            self.rfile=io.BytesIO(data)
            return super().do_POST()
        body=json.dumps({'choices':[{'message':{'content':json.dumps(result)}}]}).encode()
        self.send_response(200)
        self.send_header('Content-Length',str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def fixture_volume(original,destination):
    # Add one zero-byte .c entry before freezing the test inputs. Preserve all
    # old node IDs, including cwd and the two special source nodes.
    raw=original.read_bytes()
    count=struct.unpack_from('<I',raw,8)[0]
    end=16+16*count
    volume=bytearray(raw[:end]+bytes(16)+raw[end:])
    user=None
    for i in range(count):
        parent,name,offset,size=struct.unpack_from('<4I',raw,16+16*i)
        if parent==1 and raw[name:raw.index(0,name)]==b'user': user=i
        struct.pack_into('<4I',volume,16+16*i,parent,name+16,offset+16 if offset else 0,size)
    assert user is not None
    name=len(volume); volume.extend(b'empty.c\0')
    offset=len(volume); volume.append(0)
    struct.pack_into('<4I',volume,end,user,name,offset,0)
    struct.pack_into('<II',volume,4,len(volume),count+1)
    destination.write_bytes(volume)


def fingerprint(image,volume):
    data=volume.read_bytes()
    count=struct.unpack_from('<I',data,8)[0]
    directory=next(i for i in range(count) if _leaf(data,i)==(1,b'net'))
    idx=next(i for i in range(count) if _leaf(data,i)==(directory,b'provider.c'))
    off,n=struct.unpack_from('<II',data,16+16*idx+8)
    return tuple(hashlib.sha256(v).hexdigest() for v in (image.read_bytes(),data,data[off:off+n]))


def _leaf(data,idx):
    parent,name=struct.unpack_from('<II',data,16+16*idx)
    return parent,data[name:data.index(0,name)]


def put(c,name,contents,expected='workspace file saved'):
    expect(c,'work put /work/'+name+'\r'+contents.rstrip('\n').replace('\n','\r')+'\r.',expected)


def exercise(tmp,key):
    disk=tmp/'flow.img'
    volume=tmp/'frozen-sources.img'
    image=Path(os.environ.get('LIXINOS_BOOT_IMAGE',ROOT/'release/boot.elf'))
    fixture_volume(Path(os.environ.get('LIXINOS_SOURCE_VOLUME',ROOT/'release/sources.img')),volume)
    initial=fingerprint(image,volume)
    expected=sum(p.read_bytes().count(b'\n') for p in (ROOT/'user').glob('*.c'))
    with patch.dict(os.environ,LIXINOS_BOOT_IMAGE=str(image),LIXINOS_SOURCE_VOLUME=str(volume)), provider(False,FlowProvider) as (url,_):
        with boot(disk,timeout=300) as c:
            c(); c('agent model '+url+' fixture')
            wc=command_hash(c(MAKE),'wc')
            assert 'FLOW total:' not in c('flow list')
            expect(c,'agent task '+GOAL,'complete counts and maximum verified')
            expect(c,'result info','manifest=/src/flows/longest.flow')
        baseline=disk_kernel(disk)
        with boot(disk,timeout=300) as c:
            c()
            for name in ('sum.tests','checktotal.c','checktotal.build','total.flow'):
                put(c,name,(BUNDLE/name).read_text())
            put(c,'total.flow','name wrong','workspace file immutable')
            expect(c,'flow install /work/total.flow','installed flow total')
            expect(c,'flow install /work/total.flow','flow already installed')
            expect(c,'ls /work','total.flow')
            if key: c('agent model https://api.deepseek.com/chat/completions deepseek-flash')
        with boot(disk,timeout=420) as c:
            c(); expect(c,'flow list','FLOW total:')
            if key: c('agent key',secret=key)
            result=c('agent task '+TOTAL)
            assert 'invoke wc elf='+wc in result and 'selected flow total from /work/total.flow' in result,result
            new_sum=command_hash(result,'sum')
            assert 'verified result: '+str(expected)+'\n' in result and 'complete counts and total verified' in result,result
        with boot(disk) as c:
            c()
            expect(c,'result info','manifest=/work/total.flow')
            expect(c,'result verify','complete counts and total verified')
            expect(c,'cat /work/counts.txt','/src/user/empty.c 0\n')
            expect(c,'tcc /src/validation/omit.build','application ELF ready')
            expect(c,'run /work/counts.txt /src/user/empty.c > /work/scratch.txt','saved /work/scratch.txt')
            assert '/src/user/empty.c' not in c('cat /work/scratch.txt')
            expect(c,'cat /work/answer.txt',f'\n{expected}\n')
            expect(c,'result verify /work/scratch.txt /work/answer.txt','missing input: /src/user/empty.c\nverification rejected')
            expect(c,'flow run total','invoke sum elf='+new_sum)
            expect(c,'result verify','complete counts and total verified')
        if not key:
            # The first four workspace commits save files. The fifth marks
            # the flow installed. A torn fifth header restores the staged files.
            work_base=47105+64+8*480+40+4
            with disk.open('r+b') as f:
                f.seek((work_base+70)*512); f.write(b'BAD!')
            with boot(disk) as c:
                c()
                assert 'FLOW total:' not in c('flow list')
                expect(c,'flow run total','flow unavailable')
                expect(c,'flow install /work/total.flow','installed flow total')
                expect(c,'flow run total','verified result: '+str(expected))
        assert initial==fingerprint(image,volume) and baseline==disk_kernel(disk)
        if key: assert key.encode() not in disk.read_bytes()
        print(f'PASS: frozen boot={initial[0]} source_volume={initial[1]} provider.c={initial[2]}; total={expected}, omitted zero-byte file rejected')


def upgrade_result(tmp):
    disk=tmp/'legacy.img'
    with provider(False,FlowProvider) as (url,_), old_release(tmp,'v1.5.0'), boot(disk) as c:
        expect(c,None,'Kernel 1.5.0')
        c('agent model '+url+' fixture'); command_hash(c(MAKE),'wc')
        expect(c,'agent task '+GOAL,'complete counts and maximum verified')
    with boot(disk,launcher=[ROOT/'run.sh','--upgrade']) as c:
        expect(c,None,'Kernel 1.6.0')
        expect(c,'result info','manifest=/src/flows/longest.flow')
        expect(c,'result verify','complete counts and maximum verified')
    print('PASS: verified a result committed in v1.5 after upgrading the same disk')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--deepseek-keychain',action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        if args.deepseek_keychain:
            with provider(True) as (_,key): exercise(Path(tmp),key)
        else:
            exercise(Path(tmp),None)
            upgrade_result(Path(tmp))
