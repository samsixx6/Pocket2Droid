"""Historical prototype builder; requires omitted private compatibility caches.
Use tools/build_graphics.py for current standalone host/Android C builds.
See BUILDING.md for the Rust CPU library naming and deployment requirements.
"""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess
ROOT=Path(__file__).resolve().parent
def build(sanitize=False,target='host'):
 assert not sanitize or target=='host'
 android=target=='android-arm64-v8a'
 out=ROOT/'artifacts'/target;out.mkdir(parents=True,exist_ok=True)
 sources=[ROOT/'protocol.c',*[ROOT/'native'/n for n in ('context.c','commands.c','textures.c','renderer.c')]]
 compiler='/opt/pocket2droid-android-tools/bionic/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang' if android else 'cc' if sanitize else 'clang-17';dest=out/('libp2d_gles2_sanitize.so' if sanitize else 'libp2d_gles2.so')
 link=['-Wl,-z,max-page-size=16384','-lEGL','-ldl'] if android else ['-Wl,-l:libEGL.so.1','-ldl']
 cmd=[compiler,'-std=c11','-shared','-fPIC','-O1' if sanitize else '-O2','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wl,-Bsymbolic-functions','-I'+str(ROOT),*map(str,sources),*link,'-o',str(dest)]
 if sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(cmd,check=True)
 codec=ROOT.parent/('gles-android/native/gpu/artifacts/android-arm64-v8a/libp2d_gles_cpu.so' if android else 'gles-android/compatibility/virtual-pool/renderer/libp2d_gles_cpu.so');shutil.copyfile(codec,out/codec.name)
 inputs=[*sources,Path(__file__),*ROOT.glob('*.h'),*ROOT.glob('*.inc'),* (ROOT/'native').glob('*.h'),*(ROOT/'native').glob('*.inc')]
 record={'target':target,'compiler':compiler,'sanitize':sanitize,'files':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.so')},'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
 (out/'manifest.json').write_text(json.dumps(record,indent=2)+'\n');print('Built',dest.name)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--target',default='host',choices=['host','android-arm64-v8a']);p.add_argument('--sanitize',action='store_true');a=p.parse_args();build(a.sanitize,a.target)
