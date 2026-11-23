"""Compile and exercise the guest command suite, including durable file operations."""
from pathlib import Path
import tempfile
from test import boot, expect

def body(console, command):
    result=console(command)
    assert result.startswith(command+"\n"), result
    return result[len(command)+1:].removesuffix("kernel> ")

def exercise(disk):
    with boot(disk,timeout=60) as c:
        c()
        expect(c,'help','agent shell GOAL')
        expect(c,'commands','find [PATH]')
        assert body(c,'which sort')=='sort\n'
        cases=[
            ('echo -n "one | two"','one | two'),('seq 5 -2 1','5\n3\n1\n'),
            ('basename /src/user/mlp.c .c','mlp\n'),('dirname /src/user/','/src\n'),
            ("printf '%s\\n' pear apple apple | sort | uniq -c",'2 apple\n1 pear\n'),
            ("printf 'aa:2\\nb:1' | cut -d : -f 2",'2\n1\n'),
            ("printf 'aB9' | tr a-z A-Z | tr -d 0-9",'AB'),
            ("printf 'abc\\ndef' | wc -lwc",'1 2 7\n'),
            ("printf 'abc\\ndef' | head -n 1",'abc\n'),("printf 'abc\\ndef' | tail -n 1",'def'),
            ("echo ok | nl",'1\tok\n'),("printf 'Aa\\naA\\nX\\n' | grep -inv aa",'3:X\n'),
            ("printf abc | sha256sum","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  -\n"),
            ("printf '%s\\n' 'a b' 'c d' | xargs -n 1 -d '\\n' echo",'a b\nc d\n'),
            ("rg -l 'int main' /src/user/hello.c",'/src/user/hello.c\n'),
            ('false','exit=1\n'),('true',''),('sh test left = right','exit=1\n'),
            ('find /src/user -type f -name "*.build" | grep shell.build','/src/user/shell.build\n')]
        for cmd,want in cases:
            assert body(c,cmd)==want,cmd
        expect(c,'tcc /src/user/shell.build','application ELF ready')
        expect(c,'run echo guest-built','guest-built')
        for cmd in ['mkdir /work/project','cd /work/project','touch empty',
                    "printf '%s\\n' pear apple apple > 'input file'",
                    "sort 'input file' | uniq -c > report",'cp report copy','mv copy saved',
                    'echo done >> saved']:
            assert body(c,cmd)=='',cmd
        assert body(c,'pwd')=='/work/project\n'
        assert body(c,"cat < 'input file'")=='pear\napple\napple\n'
        assert body(c,'stat saved')=='file 20 bytes /work/project/saved\n'
        assert body(c,'cmp saved report')=='exit=1\n'
        assert body(c,'echo add | tee -a saved')=='add\n'
        assert body(c,'test -d .')==''
        assert body(c,'test -f saved')==''
        expect(c,'rmdir /work/project','directory')
        expect(c,'false > saved','exit=1')
        expect(c,'echo fail > /src/kernel.c','writable /work path')
        expect(c,'echo fail |','invalid pipeline')
        assert body(c,'cat saved')=='2 apple\n1 pear\ndone\nadd\n'
        # The ordinary guest TCC opens and compiles a file made by the new commands.
        assert body(c,"printf '/src/user/crt.c\\n/work/project/sample.c\\n' > sample.build")==''
        assert body(c,"printf '#include \"app.h\"\\nint main(int n,char **v) { puts(\"built in guest\"); return 0; }\\n' > sample.c")==''
        expect(c,'tcc /work/project/sample.build','application ELF ready')
        expect(c,'run','built in guest')
    with boot(disk) as c:
        c();assert body(c,'cd /work/project')==''
        assert body(c,'cat saved')=='2 apple\n1 pear\ndone\nadd\n'
        expect(c,'ls','saved')
        assert body(c,"awk '{print $2}' report")=='apple\npear\n'
        assert body(c,"sed -i 's/apple/banana/g' report")==''
        assert body(c,'cat report')=='2 banana\n1 pear\n'
        changes=body(c,'diff -u report saved')
        assert '-2 banana\n' in changes and '+2 apple\n' in changes and 'exit=1' in changes

        for name in ['empty','"input file"','report','saved','sample.c','sample.build']:
            assert body(c,'rm '+name)==''
        assert body(c,'cd ..')==''
        assert body(c,'rmdir project')==''
        assert body(c,'test -e project')=='exit=1\n'
        for i in range(32):
            assert body(c,f'touch /work/f{i}')==''
        expect(c,'touch /work/overflow','workspace full')
        assert body(c,'mv /work/f31 /work/final')==''
        assert body(c,'rm /work/final')==''
        assert body(c,'echo stable > /work/checkpoint')==''
        assert body(c,'echo candidate > /work/checkpoint')==''
    # Damage only the newest snapshot on this throwaway disk; the preceding one must boot.
    import struct
    base=2048+1+6*1024*1024//512
    with disk.open('r+b') as f:
        versions=[]
        for sector in (base,base+272):
            f.seek(sector*512);magic,revision=struct.unpack('<II',f.read(8))
            assert magic==0x53484631
            versions.append((revision,sector))
        f.seek(max(versions)[1]*512);f.write(b'BAD!')
    with boot(disk) as c:
        c();assert body(c,'cat /work/checkpoint')=='stable\n'

if __name__=='__main__':
    with tempfile.TemporaryDirectory() as tmp:
        exercise(Path(tmp)/'shell.img')
    print('PASS: guest TCC shell, pipelines, persistent workspace, guest-built application')
