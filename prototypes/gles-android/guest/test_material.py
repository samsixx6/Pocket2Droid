"""Actual guest material packing and ARM export ABI regressions; run in WSL."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, subprocess

HERE=Path(__file__).resolve().parent

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--stubs-only',action='store_true');a=parser.parse_args()
    out=HERE/'build/material-tests';out.mkdir(parents=True,exist_ok=True)
    spec=importlib.util.spec_from_file_location('material_builder',HERE/'build_bridge.py')
    builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
    (out/'bridge_ops.h').write_text(''.join(f'#define OP_{n} {op}\n' for n,op in builder.CM.items())+
        'static const unsigned char op_supported[141]={'+','.join('1' if i>=35 else '0' for i in range(141))+'};\n')
    results=[]
    if not a.stubs_only:
        source=(HERE/'guest_bridge.c').read_text().replace('typedef unsigned long U32;','typedef uint32_t U32;').replace('typedef long I32;','typedef int32_t I32;')
        headers='#include <stdint.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n#include <sys/mman.h>\n#define _start p2d_bridge_entry\n'
        fixture=out/'bridge.c';fixture.write_text(headers+source+'\n#undef _start\n#define main capability_main\n'+(HERE/'capability_regression.c').read_text()+'\n#undef main\n'+(HERE/'material_regression.c').read_text())
        binary=out/'material'
        subprocess.run(['clang-17','-O2','-g','-no-pie','-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast','-fshort-wchar','-I'+str(out),'-I'+str(HERE),str(fixture),'-o',str(binary)],check=True)
        run=subprocess.run([str(binary)],capture_output=True,text=True);print(run.stdout,end='');print(run.stderr,end='');results.append(dict(case='actual-guest-material',exit=run.returncode))
    # Inspect the actual compiled ARM exports, rather than testing only the
    # metadata dictionary: each function must pass three args to bridge().
    builder.make_sources(out/'arm')
    disassembly=subprocess.check_output(['/opt/pocket2droid-android-tools/bionic/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump','-d',str(out/'arm/bridge.elf')],text=True)
    for name in ['glMaterialf','glMaterialfv','glMaterialx','glMaterialxv']:
        import re
        block=disassembly.split('<'+name+'>:',1)[1].split('\n\n',1)[0]
        passed=bool(re.search(r'\bmov\s+r3, #3\b',block));print(('PASS' if passed else 'FAIL'),name,'ARM stub arity')
        results.append(dict(case=name,passed=passed,disassembly=block))
    report=dict(checks=results,sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [HERE/'build_bridge.py',HERE/'guest_bridge.c',HERE/'material_regression.c']},passed=all(r.get('passed',r.get('exit')==0) for r in results))
    (out/'RESULT.json').write_text(json.dumps(report,indent=2)+'\n');raise SystemExit(0 if report['passed'] else 1)

if __name__=='__main__':main()
