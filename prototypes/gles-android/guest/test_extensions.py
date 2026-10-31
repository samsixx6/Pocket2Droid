from pathlib import Path
import importlib.util,subprocess,json,hashlib
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('b',HERE/'build_bridge.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
out=HERE/'build/extension-tests';out.mkdir(parents=True,exist_ok=True)
(out/'bridge_ops.h').write_text(''.join(f'#define OP_{n} {op}\n' for n,op in b.CM.items())+'static const unsigned char op_supported[141]={'+','.join('1' if i>=35 else '0' for i in range(141))+'};\n')
(out/'command-fixture.c').write_text((HERE/'command_regression.c').read_text().replace('int main(int argc,char**argv)', 'int old_command_main(int argc,char**argv)'))
headers='#define _GNU_SOURCE\n#include <stdint.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n#include <sys/mman.h>\n#define _start p2d_bridge_entry\n'
raw=(HERE/'guest_bridge.c').read_text().replace('typedef unsigned long U32;','typedef uint32_t U32;').replace('typedef long I32;','typedef int32_t I32;')
(out/'bridge.c').write_text(headers+raw+'\n#undef _start\n#include "extension_regression.c"\n')
subprocess.run(['gcc','-std=c99','-O2','-g','-fshort-wchar','-fno-pie','-no-pie','-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast','-fsanitize=address,undefined','-I'+str(out),'-I'+str(HERE),str(out/'bridge.c'),'-o',str(out/'test')],check=True)
r=subprocess.run([str(out/'test')],capture_output=True,text=True);print(r.stdout,r.stderr)
(out/'RESULT.json').write_text(json.dumps(dict(exit=r.returncode,stdout=r.stdout,stderr=r.stderr,source_sha256=hashlib.sha256((HERE/'guest_bridge.c').read_bytes()).hexdigest()),indent=2))
assert r.returncode==0
