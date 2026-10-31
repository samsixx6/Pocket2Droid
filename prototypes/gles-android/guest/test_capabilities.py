"""Execute actual guest initialization/capabilities and setup's mode parser."""
from pathlib import Path
import hashlib, importlib.util, json, subprocess

HERE=Path(__file__).resolve().parent


def main():
    out=HERE/'build/capability-tests';out.mkdir(parents=True,exist_ok=True)
    spec=importlib.util.spec_from_file_location('caps_bridge_builder',HERE/'build_bridge.py')
    builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
    (out/'bridge_ops.h').write_text(''.join(f'#define OP_{n} {op}\n' for n,op in builder.CM.items())+
        'static const unsigned char op_supported[141]={'+','.join('1' if i>=35 else '0' for i in range(141))+'};\n')
    source=(HERE/'guest_bridge.c').read_text().replace('typedef unsigned long U32;','typedef uint32_t U32;').replace('typedef long I32;','typedef int32_t I32;')
    headers='#include <stdint.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n#include <sys/mman.h>\n#define _start p2d_bridge_entry\n'
    fixture=out/'bridge.c';fixture.write_text(headers+source+'\n#undef _start\n'+(HERE/'capability_regression.c').read_text())
    binary=out/'bridge'
    subprocess.run(['clang-17','-O2','-no-pie','-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast','-fshort-wchar','-I'+str(out),'-I'+str(HERE),str(fixture),'-o',str(binary)],check=True)
    # Literal expected limits catch a missing mode read, permissive prefix parse,
    # inconsistent validators, re-reading per call, and failure to bound the file.
    cases=[('missing',None,0,0,0),('old-on',b'on',0,0,2),('gpu',b'on gpu',0,0,2),
        ('cpu',b'on cpu',0,0,4),('cpu-crlf',b'on cpu\r\n',0,0,4),('cpu-chunked',b'on cpu',2,0,4),
        ('cpu-sixteen-bytes',b'on cpu'+b' '*10,0,0,4),('empty',b'',0,0,0),('off',b'off',0,0,0),
        ('cpu-junk',b'on cpux',0,0,0),('gpu-junk',b'on gpu junk',0,0,0),('unknown',b'on auto',0,0,0),
        ('uppercase',b'on CPU',0,0,0),('leading-space',b' on cpu',0,0,0),('embedded-nul',b'on cpu\0',0,0,0),
        ('over-limit',b'on cpu'+b' '*11,0,0,0),('hidden-tail',b'on cpu'+b' '*10+b'x',2,0,0),
        ('read-fails',b'on cpu',0,1,0),('partial-read-fails',b'on cpu',2,2,0)]
    rows=[]
    for name,data,chunk,fail,want in cases:
        r=subprocess.run([str(binary),'-' if data is None else data.hex(),str(chunk),str(fail),str(want)],capture_output=True,text=True)
        rows.append(dict(case=name,passed=r.returncode==0,exit=r.returncode,stdout=r.stdout,stderr=r.stderr))
    setup=(HERE/'setup_ce.c').read_text();start=setup.index('static int mode(void)');end=setup.index('\nstatic void result',start)
    # Compile the production parser unchanged, with only its platform API mocked.
    parser=out/'setup-mode.c';parser.write_text('''#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t U32;typedef unsigned short W;typedef void *H;
#define ROOT L"\\\\Storage Card\\\\P2DGLES\\\\"
static const char *input;static U32 offset,chunk,reads,fail;
H CreateFileW(const W *p,U32 a,U32 b,void *c,U32 d,U32 e,H f){(void)p;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return (H)1;}
int ReadFile(H h,void *p,U32 n,U32 *got,void *o){(void)h;(void)o;*got=0;if(++reads==fail)return 0;U32 left=strlen(input)-offset;if(n>left)n=left;if(chunk&&n>chunk)n=chunk;memcpy(p,input+offset,n);offset+=n;*got=n;return 1;}
int CloseHandle(H h){(void)h;return 1;}
'''+setup[start:end]+'''
int main(int argc,char **argv){assert(argc==5);input=argv[1];chunk=atoi(argv[2]);fail=atoi(argv[3]);int got=mode(),want=atoi(argv[4]);if(got!=want){fprintf(stderr,"mode() returned %d, expected %d\\n",got,want);return 1;}puts("PASS");return 0;}
''')
    parser_binary=out/'setup-mode'
    subprocess.run(['clang-17','-O2','-fshort-wchar',str(parser),'-o',str(parser_binary)],check=True)
    setup_cases=[('on','on',0,0,1),('gpu','on gpu',0,0,1),('cpu','on cpu',0,0,1),('off','off',0,0,0),
        ('cpu-crlf','on cpu\r\n',0,0,1),('gpu-chunked','on gpu',2,0,1),('sixteen','on cpu'+' '*10,0,0,1),
        ('unknown','on auto',0,0,-1),('junk','on cpu junk',0,0,-1),('off-junk','off gpu',0,0,-1),
        ('over-limit','on cpu'+' '*11,0,0,-1),('partial-fail','on cpu',2,2,-1),('empty','',0,0,-1)]
    for name,data,chunk,fail,want in setup_cases:
        r=subprocess.run([str(parser_binary),data,str(chunk),str(fail),str(want)],capture_output=True,text=True)
        rows.append(dict(case='setup-'+name,passed=r.returncode==0,exit=r.returncode,stdout=r.stdout,stderr=r.stderr))
    report=dict(bridge_sha256=hashlib.sha256((HERE/'guest_bridge.c').read_bytes()).hexdigest(),
        setup_sha256=hashlib.sha256((HERE/'setup_ce.c').read_bytes()).hexdigest(),checks=rows,passed=all(r['passed'] for r in rows))
    (out/'RESULT.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2));raise SystemExit(0 if report['passed'] else 1)


if __name__=='__main__':main()
