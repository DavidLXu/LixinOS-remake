"""Assemble the guest source volume and its ordered, guest-readable link recipe."""
from pathlib import Path
import re
import struct
import subprocess
import sys
import tarfile

root=Path.cwd()
build=root/'build'
files={}
def add(name, path):
    files[name]=path.read_bytes()
def tree(name, path, suffixes):
    for p in sorted(path.rglob('*')):
        if p.is_file() and p.suffix in suffixes and not re.search(r' \d+\.',p.name):
            add(name+'/'+str(p.relative_to(path)),p)
add('kernel.c',build/'kernel.c')
add('tools.c',build/'tools.c')
tree('kernel',root/'src',{'.c','.h'})
tree('user',root/'user',{'.c','.h','.build'})
tree('validation',root/'validation',{'.c','.build'})
tree('boot',root/'boot',{'.h','.c','.S','.ld'})
tree('net',root/'net',{'.c','.h'})
tiny=build/'tinycc'
tracked=subprocess.check_output(['git','-C','deps/tinycc','ls-tree','-r','--name-only',
    'f830a1697e159056b75de40a60464bc43ee670a3'],text=True).splitlines()
for name in tracked:
    if (('/' not in name or name.startswith(('include/','lib/'))) and
        (Path(name).suffix in {'.c','.h','.S','.def'} or name in ('COPYING','RELICENSING'))):
        add('tinycc/'+name,tiny/name)
add('tinycc/tccdefs_.h',tiny/'tccdefs_.h')
add('tinycc/config.h',root/'boot/config.h')
for archive, prefix, label in [('lwip-src.tar.gz','lwip/','lwip/'),
                               ('bearssl-0.6.tar.gz','bearssl-0.6/','bearssl/')]:
    with tarfile.open(root/'third_party'/archive) as tar:
        for entry in tar:
            if not entry.isfile() or not entry.name.startswith(prefix): continue
            rel=entry.name[len(prefix):]
            if rel.startswith('src/') and label=='lwip/': rel=rel[4:]
            elif label=='bearssl/' and not rel.startswith(('inc/','src/')) and rel!='LICENSE.txt': continue
            if Path(rel).suffix in ('.c','.h') or rel in ('LICENSE.txt','COPYING'):
                files[label+rel]=tar.extractfile(entry).read()
for name in ('cJSON.c','cJSON.h'): add('cjson/'+name,root/'third_party'/name)
tree('bootstrap',root/'bootstrap',{'.h','.c','.S','.s'})
musl=build/'musl-1.2.5'
tree('sdk',musl/'include',{'.h'})
tree('sdk/bits',musl/'arch/generic/bits',{'.h'})
tree('sdk/bits',musl/'arch/aarch64/bits',{'.h'})
files['sdk/bits/alltypes.h']=subprocess.check_output(['sed','-f',musl/'tools/mkalltypes.sed',
    musl/'arch/aarch64/bits/alltypes.h.in',musl/'include/alltypes.h.in'])
add('musl/COPYRIGHT',musl/'COPYRIGHT')
libfiles=('string/memcpy string/memmove string/memset string/memcmp string/memchr string/strlen '
          'string/strnlen string/strcpy string/stpcpy string/strcmp string/strncmp string/strchr '
          'string/strchrnul string/strrchr string/memrchr string/strstr string/strcspn '
          'string/strcasecmp string/strncasecmp string/strncpy string/stpncpy string/strcat '
          'stdlib/atoi stdlib/strtod stdlib/strtol internal/floatscan internal/intscan '
          'time/__secs_to_tm ctype/isspace ctype/isxdigit ctype/tolower math/__fpclassifyl math/scalbn math/scalbnl math/copysignl math/fabsl math/fmodl')
for name in libfiles.split():
    add('musl/'+name+'.c',musl/('src/'+name+'.c'))
for name in ('libm.h','floatscan.h','intscan.h','shgetc.h'):
    add('musl/internal/'+name,musl/('src/internal/'+name))
add('musl/internal/fp_arch.h',musl/'arch/aarch64/fp_arch.h')
# Preserve the upstream source and headers alongside each locally ported component.
units=['boot/guest-start.S','boot/platform.c','boot/resident.c','tinycc/libtcc.c','tinycc/lib/lib-arm64.c',
       'bootstrap/runtime.c','bootstrap/scan.c','bootstrap/jump.S','net/transport.c','net/provider.c','cjson/cJSON.c']
units+=['musl/'+n+'.c' for n in libfiles.split()]
units+=sorted(n for n in files if re.fullmatch(r'lwip/core/(ipv4/)?[^/]+\.c',n))
units+=['lwip/netif/ethernet.c']
units+=sorted(n for n in files if n.startswith('bearssl/src/') and n.endswith('.c'))
apps=sorted('/src/'+n for n in files if n.startswith('user/') and n.endswith('.build'))
files['apps.build']=('\n'.join(['/src/tools.c']+apps)+'\n').encode()
files['system.build']=''.join('/src/'+n+'\n' for n in units).encode()
# Expose SDK paths at their guest locations when debugging the compiler off target.
if '--extract' in sys.argv:
    for name,data in files.items():
        path=build/'sysroot/src'/name; path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
paths=['/','/src','/src/kernel.c','/src/tools.c']
index={p:i for i,p in enumerate(paths)}
for rel in sorted(files):
    path='/src'
    for part in rel.split('/'):
        path+='/'+part
        if path not in index: index[path]=len(paths); paths.append(path)
volume=bytearray(16+16*len(paths))
for i,path in enumerate(paths):
    parent=index.get(str(Path(path).parent),0)
    name=len(volume); volume.extend(Path(path).name.encode()+b'\0')
    data=files.get(path[5:]) if path.startswith('/src/') else None
    start=len(volume) if data is not None else 0
    if data is not None: volume.extend(data+b'\0')
    struct.pack_into('<4I',volume,16+16*i,parent,name,start,len(data or b''))
assert len(volume)<=32*1024*1024
struct.pack_into('<4I',volume,0,0x53524331,len(volume),len(paths),1)
(build/'sources.img').write_bytes(volume)
print(f'{len(files)} source files, {len(units)} build inputs, {len(volume)} bytes')
