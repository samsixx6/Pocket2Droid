"""Reproducible isolated Android GLES CE assets; invoke inside WSL."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, shutil, struct, subprocess, sys
HERE=Path(__file__).resolve().parent
EXP=HERE.parents[2]
sys.path.insert(0,str(EXP/'guest/network-setup'))
import build as ce
spec=importlib.util.spec_from_file_location('bridge_builder',HERE/'build_bridge.py')
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
OUT=HERE/'build'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def generate_apis():
    rows=[]
    for name in sorted(bridge.ARITIES):
        ords=[next((int(o) for o,n in bridge.TABLES[p].items() if n==name),0) for p in ('cl','cm')]
        if any(ords):rows.append((name,*ords))
    (HERE/'supported_apis.h').write_text('/* Generated from proven bridge arities + official ordinal maps. */\n'
        'typedef struct {const char *name;unsigned short cl,cm;} P2DApi;\n'
        'static const P2DApi p2d_apis[]={\n'+''.join(f' {{"{n}",{cl},{cm}}},\n' for n,cl,cm in rows)+
        '};\n#define P2D_API_COUNT (sizeof(p2d_apis)/sizeof(p2d_apis[0]))\n')
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--tests-only',action='store_true');args=parser.parse_args()
    OUT.mkdir(exist_ok=True);generate_apis()
    subprocess.run(['clang-17','-std=c99','-Wall','-Wextra','-Werror','-O2',str(HERE/'setup_core.c'),str(HERE/'session.c'),str(HERE/'host_test.c'),'-o',str(OUT/'host-test')],check=True)
    subprocess.run([sys.executable,str(HERE/'test_guest.py')],check=True)
    subprocess.run([sys.executable,str(HERE/'test_capabilities.py')],check=True)
    subprocess.run([sys.executable,str(HERE/'test_lighting.py')],check=True)
    if args.tests_only:return
    bridge.build(OUT/'bridge')
    for name in ('libGLES_CL.dll','libGLES_CM.dll'):shutil.copyfile(OUT/'bridge'/name,OUT/name)
    for source,name in [('setup_ce.c','setup.exe'),('autorun.c','autorun.exe')]:
        folder=OUT/name.split('.')[0];ce.build(folder,HERE/source);shutil.copyfile(folder/'autorun.exe',OUT/name)
    names=('autorun.exe','setup.exe','libGLES_CL.dll','libGLES_CM.dll')
    sources=sorted(p for p in HERE.iterdir() if p.suffix in ('.c','.h','.py'))
    manifest={'files':{n:sha(OUT/n) for n in names},'sources':{p.name:sha(p) for p in sources},
      'format':1,'machine':'ARMv4','subsystem':9,'mode_file':'mode.txt','mode_max_bytes':16,'mode_values':['on','on gpu','on cpu','off'],
      'texture_units':{'default':2,'on':2,'on gpu':2,'on cpu':4},
      'optional_network':'network.exe','session_reset':{'file':'session.reset','bytes':4,'format':'little-endian nonzero process/session ID'},
      'builder':sha(HERE/'build.py'),'baseline_bridge':{'source':str(EXP/'prototypes/gles-support/bridge-lab/guest_bridge.c'),'sha256':sha(EXP/'prototypes/gles-support/bridge-lab/guest_bridge.c')},
      'renderer':'Pocket2Droid native GLES bridge; GPU preferred with startup CPU fallback or explicit CPU mode',
      'limitations':['single process/context','mode capabilities fixed at DLL initialization','static known CL/CM imports only','periodic scans bounded; dynamically loaded-only GLES not guaranteed']}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    subprocess.run([sys.executable,str(HERE/'verify_assets.py')],check=True)
    print(json.dumps(manifest['files'],indent=2))
if __name__=='__main__':main()
