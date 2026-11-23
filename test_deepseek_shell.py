"""Compare the model-authored and original shells on separate disposable guest disks."""
from pathlib import Path
import tempfile
from test import boot, expect

CASES = [
    'commands', 'which head', 'commands sort', 'which absent',
    'echo -n "two words" end', 'true', 'false', 'seq 5 -2 0',
    "printf '%s\\t%s\\n' one two three four", 'printf "100%%\\n"',
    'basename /src/user/shell.c .c','basename /','dirname abc','dirname /src/user/',
    'pwd','mkdir /work/data','cd /work/data','pwd', 'touch empty','stat empty',
    "printf 'Beta:9\\nalpha:1\\nalpha:1\\nend:8' > a",
    'cat a empty','wc -lwc a','head -n 2 a','tail -n 1 a', 'tail -n 0 a', 'nl a',
    'grep -inc ALPHA a','grep -v end a','grep nope a',
    'sort -ru a','sort a | uniq -c','cut -d : -f 2 a',
    "printf '  two  words\\nx\\n' | awk '{print $2}'",
    "printf '  two  words\\nx\\n' | awk '{print NF}'",
    "printf '  two  words\\nx\\n' | awk '{print NR}'",
    'cat a | tr a-z A-Z | tr -d 0-9',
    'cp a b','cp a b','cmp a b','touch copy','mv b copy','test -e b','test -d .','test -f copy',
    'echo extra | tee -a copy','cmp a copy','diff -u a copy',
    "sed 's/a/X/g' a","sed -i 's/end/finish/g' copy",'cat copy','sha256sum a',
    'find . -type f -name "c*"','ls .','rg -il FINISH .','rg -i alpha a',
    "printf '%s\\n' 'two words' three | xargs -d '\\n' -n 1 echo",
    'cat a | sort | uniq -c','rm empty','rm a','rm copy','cd ..','rmdir data',
    'test -e data','echo persisted > /work/probe',
    "printf '' | wc", "printf 'x' | grep ''", "printf 'x\\nx\\ny' | uniq -c",
    "printf 'no_delimiter\\na:b' | cut -d : -f 2", "printf 'a\\na' | sed 's/a//g'",
    "printf 'a\\nb' | head -n 0", "printf 'a\\nb' | tail -n 20",
    "printf '' > /work/left", "printf 'a' > /work/right", "diff /work/left /work/right",
    "diff /work/right /work/left", "diff /work/right /work/right",
    "sha256sum /work/left", "printf 'b a a' | xargs -n 2 echo",
    'find /work -type f -name "r?ght"','rg -l zzz /work/right',
    'dirname /', 'basename /work/sample.txt .txt', 'seq 0', 'seq 3 1',
    "seq 1500 | sed 's/1/X/g' | wc -c", 'seq 1500 | sort -r | head -n 3',
    'seq 1500 | tail -n 2', "seq 1500 | awk '{print NR}' | tail -n 1",
    "printf 'aaAb' | tr -d a-z", "printf 'xyz' | tr x-z q",
    'test a = a', 'test a = b', 'test -f /work', 'cd /', 'pwd'
]
COLD=['cat /work/probe','printf abc | sha256sum','find /src/user/deepseek -type f -name "*.c" | wc -l']

def run(shell,disk):
    output={}
    for cases in [CASES,COLD]:
        with boot(disk,timeout=90) as c:
            c()
            expect(c,'tcc /src/user/'+('shell' if shell=='sh' else 'deepseek')+'.build','application ELF ready')
            for i,case in enumerate(cases):
                cmd=shell+' '+case
                reply=c(cmd)
                assert reply.startswith(cmd+'\n')
                output[(cases is COLD,i,case)]=reply[len(cmd)+1:].removesuffix('kernel> ')
    return output

if __name__=='__main__':
    with tempfile.TemporaryDirectory() as tmp:
        a=run('sh',Path(tmp)/'reference.img')
        b=run('dsh',Path(tmp)/'deepseek.img')
    mismatches=[]
    for key in a:
        if a[key]!=b[key]:
            mismatches.append(key);print('MISMATCH',key[2],repr(a[key]),repr(b[key]))
    assert not mismatches,mismatches
    assert list(b.values())[-3:] == ['persisted\n','ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  -\n','18\n']
    print(f'PASS: 39 commands, {len(a)} differential cases including fresh cold boot')
