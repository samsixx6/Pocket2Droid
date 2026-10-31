"""Run actual guest bridge routing against mocked CE I/O/GDI with ASan/UBSan."""
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
    out = HERE / 'build/presentation-bridge-tests'
    out.mkdir(parents=True, exist_ok=True)
    source = args.source or HERE / 'guest_bridge.c'
    raw = source.read_text()
    spec = importlib.util.spec_from_file_location('presentation_bridge_builder', HERE / 'build_bridge.py')
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
    adapted = raw.replace('typedef unsigned long U32;', 'typedef uint32_t U32;').replace('typedef long I32;', 'typedef int32_t I32;')
    # The frozen bridge does not contain the module; including it for the host
    # mock helpers cannot alter its original flush/present implementation.
    helper = '' if '#include "presentation.c"' in raw else '\n#include "presentation.c"\n'
    fixture = out / ('red-bridge.c' if args.red else 'bridge.c')
    fixture.write_text(headers + adapted + '\n#undef _start\n' + helper + '\n#include "test_presentation_bridge.c"\n')
    binary = out / ('red' if args.red else 'bridge')
    subprocess.run(['gcc', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-misleading-indentation', '-Wno-pointer-to-int-cast', '-Wno-int-to-pointer-cast',
                    '-fshort-wchar', '-fno-pie', '-no-pie', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(out), '-I' + str(HERE), str(fixture), '-o', str(binary)], check=True)
    cases = ['direct'] if args.red else ['direct', 'odd', 'pbuffer', 'lifecycle', 'transfer-failure',
                                         'partial-transfer-failure', 'prepare-failure', 'missing-gdi',
                                         'blit-failure', 'present-dc-failure', 'header-failure',
                                         'large-window', 'large-odd']
    rows = []
    for case in cases:
        run = subprocess.run([str(binary), case], capture_output=True, text=True)
        rows.append(dict(case=case, exit=run.returncode, stdout=run.stdout, stderr=run.stderr))
        print(case, run.returncode, run.stdout, run.stderr)
    if args.red:
        assert rows[0]['exit'] != 0 and 'response payload must be read directly into a prepared DIB' in rows[0]['stderr']
    else:
        assert all(row['exit'] == 0 for row in rows), 'Actual bridge routing regression'
    report = dict(source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  red=args.red, passed=True, checks=rows)
    (out / ('RED.json' if args.red else 'RESULT.json')).write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
