"""Focused actual presentation-module ownership/pixel tests; invoke inside WSL."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--red', action='store_true')
    args = parser.parse_args()
    out = HERE / 'build/presentation-tests'
    out.mkdir(parents=True, exist_ok=True)
    source = HERE / 'presentation.c'
    if args.red:
        source = out / 'missing-presentation.c'
        source.write_text('''#include "presentation.h"
void p2d_present_init(P2DPresentation*p,const P2DPresentOps*o){*p=(P2DPresentation){0};p->ops=*o;}
int p2d_present_prepare(P2DPresentation*p,void*w,unsigned x,unsigned y,void**b,unsigned*s){(void)p;(void)w;(void)x;(void)y;*b=0;*s=0;return 0;}
int p2d_present_blit(P2DPresentation*p,void*w,const void*b){(void)p;(void)w;(void)b;return 0;}
void p2d_present_invalidate(P2DPresentation*p){(void)p;}
''')
    binary = out / ('red' if args.red else 'presentation')
    command = ['gcc', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-fno-pie', '-no-pie',
               '-I' + str(HERE), str(source), str(HERE / 'test_presentation.c'), '-o', str(binary)]
    subprocess.run(command, check=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    print(run.stdout, end='')
    print(run.stderr, end='')
    report = dict(red=args.red, exit=run.returncode, stdout=run.stdout, stderr=run.stderr,
                  sources={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in [source, HERE / 'presentation.h', HERE / 'test_presentation.c']})
    if args.red:
        assert run.returncode != 0 and 'p2d_present_prepare' in run.stderr
        report['expected_missing_implementation_failure'] = True
    else:
        assert run.returncode == 0, 'Presentation regression failure'
        arm = out / 'presentation-arm.o'
        arm_flags = ['clang-17', '--target=armv4t-none-eabi', '-mcpu=arm920t', '-marm',
                     '-std=c99', '-Os', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra', '-Werror']
        subprocess.run(arm_flags + ['-c', str(source), '-o', str(arm)], check=True)
        symbols = subprocess.check_output(['readelf', '-Ws', str(arm)], text=True)
        imports = [line for line in symbols.splitlines() if ' UND ' in line and len(line.split()) > 7]
        assert not imports, 'Unexpected freestanding runtime dependency: ' + repr(imports)
        report['arm_compile_no_runtime_imports'] = True
        subprocess.run(arm_flags + ['-S', str(source), '-o', str(out / 'presentation-arm.s')], check=True)
    (out / ('RED.json' if args.red else 'RESULT.json')).write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
