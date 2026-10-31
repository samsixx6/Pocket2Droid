from pathlib import Path
import argparse,os,subprocess,json,hashlib
R=Path(__file__).resolve().parents[1];P=R/'prototypes';p=argparse.ArgumentParser();p.add_argument('--target',choices=['host','android'],default='host');a=p.parse_args();android=a.target=='android';out=R/'android/build/current-libs'/a.target;out.mkdir(parents=True,exist_ok=True)
cc=str(Path(os.environ['P2D_NDK_BIN'])/'aarch64-linux-android24-clang') if android else os.environ.get('CC','clang-17')
common=[cc,'-std=c11','-shared','-fPIC','-O2','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wl,-Bsymbolic-functions']+(['-Wl,-z,max-page-size=16384'] if android else [])
gpu=P/'gles-android/native/gpu';g2=P/'gles2';link=['-lEGL','-ldl'] if android else ['-Wl,-l:libEGL.so.1','-ldl'];commands=[]
def build(name,sources,flags):
 cmd=common+list(map(str,sources))+flags+['-o',str(out/name)];subprocess.run(cmd,check=True);commands.append(cmd)
build('libp2d_gles.so',[gpu/'selector.c'],['-ldl'])
build('libp2d_gles_gpu.so',[gpu/n for n in ['renderer.c','pvrtc.c','dxt35.c']],(['-DP2D_ANDROID_PROBE'] if android else ['-DP2D_GL_SURROGATE=1'])+link)
build('libp2d_gles2.so',[g2/'protocol.c',*[g2/'native'/n for n in ['context.c','commands.c','textures.c','renderer.c']]],['-I'+str(g2)]+link)
record=dict(target=a.target,commands=commands,files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.so')});(out/'BUILD.json').write_text(json.dumps(record,indent=2)+'\n');print('Built selector, GLES1 GPU and GLES2:',out)
