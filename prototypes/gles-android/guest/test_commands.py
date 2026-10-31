"""Test the actual guest command path, with protected buffers and ASan/UBSan."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import subprocess

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--red', action='store_true')
    parser.add_argument('--source', type=Path)
    args = parser.parse_args()
    out = HERE / 'build/command-tests'
    out.mkdir(parents=True, exist_ok=True)
    source = args.source or HERE / 'guest_bridge.c'
    spec = importlib.util.spec_from_file_location('command_builder', HERE / 'build_bridge.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    (out / 'bridge_ops.h').write_text(''.join(f'#define OP_{n} {op}\n' for n, op in builder.CM.items()) +
        'static const unsigned char op_supported[141]={' + ','.join('1' if i >= 35 else '0' for i in range(141)) + '};\n')
    headers = '''#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#define _start p2d_bridge_entry
'''
    raw = source.read_text().replace('typedef unsigned long U32;', 'typedef uint32_t U32;').replace('typedef long I32;', 'typedef int32_t I32;')
    fixture = out / ('red.c' if args.red else 'bridge.c')
    fixture.write_text(headers + raw + '\n#undef _start\n#include "command_regression.c"\n')
    binary = out / ('red' if args.red else 'bridge')
    subprocess.run(['gcc', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-misleading-indentation', '-Wno-pointer-to-int-cast', '-Wno-int-to-pointer-cast',
                    '-fshort-wchar', '-fno-pie', '-no-pie', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(out), '-I' + str(HERE), str(fixture), '-o', str(binary)], check=True)
    cases = ['repeat'] if args.red else ['repeat', 'units', 'invalid', 'boundaries', 'failed-record', 'capacity-error', 'four-units', 'bulk']
    results = []
    for case in cases:
        run = subprocess.run([str(binary), case], capture_output=True, text=True)
        results.append(dict(case=case, exit=run.returncode, stdout=run.stdout, stderr=run.stderr))
        print(case, run.returncode, run.stdout, run.stderr)
    if args.red:
        assert results[0]['exit'] != 0 and 'repeated valid state must avoid' in results[0]['stderr']
    else:
        assert all(row['exit'] == 0 for row in results)
    sources = [source, HERE / 'command_regression.c', HERE / 'test_commands.py']
    sources += [p for p in (HERE / 'command_cache.h', HERE / 'bulk.h') if p.exists()]
    report = dict(red=args.red, passed=True, checks=results,
                  sources={str(p.relative_to(HERE)) if p.is_relative_to(HERE) else str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources})
    (out / ('RED.json' if args.red else 'RESULT.json')).write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
