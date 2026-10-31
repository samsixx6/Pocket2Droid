"""Actual freestanding framebuffer code under sanitizers and ARMv4 compiler."""
from pathlib import Path
import argparse,hashlib,json,subprocess
HERE=Path(__file__).resolve().parent
def main():
    a=argparse.ArgumentParser();a.add_argument('--red',action='store_true');args=a.parse_args()
    out=HERE/'build/framebuffer-tests';out.mkdir(parents=True,exist_ok=True)
    source=HERE/'framebuffer.c'
    if args.red:
        source=out/'missing.c';source.write_text('#include "framebuffer.h"\nint p2d_fb_copy(const P2DFBRaw*r,unsigned c,unsigned q,const void*s,unsigned n,unsigned w,unsigned h,unsigned p){(void)r;(void)c;(void)q;(void)s;(void)n;(void)w;(void)h;(void)p;return 0;}\n')
    exe=out/('red' if args.red else 'framebuffer')
    subprocess.run(['gcc','-std=c99','-O2','-g','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(HERE),str(source),str(HERE/'test_framebuffer.c'),'-o',str(exe)],check=True)
    run=subprocess.run([str(exe)],capture_output=True,text=True);print(run.stdout+run.stderr)
    if args.red:assert run.returncode and 'valid framebuffer copy must render' in run.stderr
    else:
        assert run.returncode==0
        arm=out/'framebuffer-arm.o'
        subprocess.run(['clang-17','--target=armv4t-none-eabi','-mcpu=arm920t','-marm','-Os','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-c',str(source),'-o',str(arm)],check=True)
        symbols=subprocess.check_output(['readelf','-Ws',str(arm)],text=True)
        assert not [s for s in symbols.splitlines() if ' UND ' in s and len(s.split())>7]
    record=dict(red=args.red,exit=run.returncode,stdout=run.stdout,stderr=run.stderr,sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [source,HERE/'framebuffer.h',HERE/'test_framebuffer.c']})
    (out/('RED.json' if args.red else 'RESULT.json')).write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
