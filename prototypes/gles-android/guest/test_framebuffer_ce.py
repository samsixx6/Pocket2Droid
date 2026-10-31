from pathlib import Path
import argparse,hashlib,json,subprocess
HERE=Path(__file__).resolve().parent
def main():
    p=argparse.ArgumentParser();p.add_argument('--red',action='store_true');a=p.parse_args()
    out=HERE/'build/framebuffer-ce-tests';out.mkdir(parents=True,exist_ok=True);source=HERE/'framebuffer_ce.c'
    if a.red:
        source=out/'missing.c';source.write_text('#include "framebuffer_ce.h"\nint p2d_fb_present(const P2DFBOps*o,void*w,const void*s,unsigned n,unsigned x,unsigned y){(void)o;(void)w;(void)s;(void)n;(void)x;(void)y;return 0;}\n')
    exe=out/('red' if a.red else 'framebuffer-ce')
    subprocess.run(['gcc','-std=c99','-O2','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(HERE),str(HERE/'framebuffer.c'),str(source),str(HERE/'test_framebuffer_ce.c'),'-o',str(exe)],check=True)
    r=subprocess.run([str(exe)],capture_output=True,text=True);print(r.stdout+r.stderr)
    if a.red:assert r.returncode and 'validated fullscreen presentation must draw' in r.stderr
    else:
        assert r.returncode==0
        subprocess.run(['clang-17','--target=armv4t-none-eabi','-mcpu=arm920t','-marm','-Os','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-c',str(source),'-o',str(out/'framebuffer-ce-arm.o')],check=True)
    (out/('RED.json' if a.red else 'RESULT.json')).write_text(json.dumps(dict(red=a.red,exit=r.returncode,stdout=r.stdout,stderr=r.stderr,sources={q.name:hashlib.sha256(q.read_bytes()).hexdigest() for q in [source,HERE/'framebuffer_ce.h',HERE/'test_framebuffer_ce.c',HERE/'framebuffer.c']}),indent=2)+'\n')
if __name__=='__main__':main()
