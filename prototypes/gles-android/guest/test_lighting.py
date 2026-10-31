"""Focused actual-bridge lighting serialization and compiled ARM stub checks."""
from pathlib import Path
import importlib.util,json,re,subprocess
HERE=Path(__file__).resolve().parent;out=HERE/'build/lighting-tests';out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('builder',HERE/'build_bridge.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
(out/'bridge_ops.h').write_text(''.join(f'#define OP_{n} {op}\n' for n,op in b.CM.items())+'static const unsigned char op_supported[141]={'+','.join('1' if i>=35 else '0' for i in range(141))+'};\n')
source=(HERE/'guest_bridge.c').read_text().replace('typedef unsigned long U32;','typedef uint32_t U32;').replace('typedef long I32;','typedef int32_t I32;')
headers='#include <stdint.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n#include <sys/mman.h>\n#define _start p2d_bridge_entry\n'
fixture=out/'test.c';fixture.write_text(headers+source+'\n#undef _start\n#define main old_main\n'+(HERE/'capability_regression.c').read_text()+'\n#undef main\n'+(HERE/'lighting_regression.c').read_text())
subprocess.run(['clang-17','-O2','-g','-no-pie','-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast','-fshort-wchar','-I'+str(out),'-I'+str(HERE),str(fixture),'-o',str(out/'test')],check=True)
subprocess.run([str(out/'test')],check=True)
b.make_sources(out/'arm');dump=subprocess.check_output(['/opt/pocket2droid-android-tools/bionic/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump','-d',str(out/'arm/bridge.elf')],text=True)
for n in ('glLightModelf','glLightModelfv','glLightModelx','glLightModelxv','glLightf','glLightfv','glLightx','glLightxv'):
 arity=2 if 'Model' in n else 3;block=dump.split('<'+n+'>:',1)[1].split('\n\n',1)[0];assert re.search(r'\bmov\s+r3, #'+str(arity)+r'\b',block),n
print('PASS eight compiled ARM lighting stub arities')
