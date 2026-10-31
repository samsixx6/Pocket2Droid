"""Build the selector/GLES GPU libraries; retain the exact accepted CPU fallback."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess
HERE=Path(__file__).resolve().parent
NATIVE=HERE.parent
NDK=Path('/opt/pocket2droid-android-tools/bionic/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64/bin')
CPU={'host':'2ef8841407ff40a293426f539da6bc418d946d8b7434355ffaa8f0e53d5c6dbc',
     'android-arm64-v8a':'7972c8fbbf27d6da719b89360107aeefc06ae1e216befa20e13ca63b6ade6d9c'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def build(target,selector_only=False,material_cpu=True):
    out=HERE/'artifacts'/target;out.mkdir(parents=True,exist_ok=True)
    android=target=='android-arm64-v8a'
    compiler=str(NDK/'aarch64-linux-android24-clang') if android else 'clang-17'
    common=[compiler,'-std=c11','-D_POSIX_C_SOURCE=200809L','-shared','-fPIC','-O2','-g','-Wall','-Wextra','-Werror','-Wl,-Bsymbolic-functions']
    if android:common+=['-Wl,-z,max-page-size=16384']
    subprocess.run(common+[str(HERE/'selector.c'),'-ldl','-o',str(out/'libp2d_gles.so')],check=True)
    if selector_only:return
    gpu_args=['-DP2D_ANDROID_PROBE','-lEGL','-ldl'] if android else ['-Wl,-l:libEGL.so.1','-ldl']
    subprocess.run(common+[str(HERE/'renderer.c'),str(HERE/'pvrtc.c'),*gpu_args,'-o',str(out/'libp2d_gles_gpu.so')],check=True)
    original=NATIVE/'artifacts'/target/'libp2d_gles.so'
    assert sha(original)==CPU[target],'Accepted CPU renderer changed'
    cpu_record=None
    if material_cpu:
        cpu_record=json.loads((HERE/'cpu-build/manifest.json').read_text())
        project=NATIVE.parent.parents[3]
        for name,expected in cpu_record['sources'].items():
            source=(project/name).resolve(strict=True)
            assert source.is_relative_to(project) and sha(source)==expected,'Stale CPU material source: '+name
        cpu=HERE/'cpu-build'/target/'libp2d_gles_cpu.so'
        assert sha(cpu)==cpu_record['files'][target]
    else:cpu=original
    shutil.copyfile(cpu,out/'libp2d_gles_cpu.so')
    source_files=[NATIVE.parent/'guest/compact.h',HERE/'selector.c',HERE/'renderer.c',Path(__file__),NATIVE/'libp2d_gles.h',NATIVE.parent/'guest/query.h']
    source_files += [HERE/name for name in ('pvrtc.c','pvrtc.h','PVRTC-LICENSE-MIT.txt','PVRTC-PROVENANCE.json')]
    if material_cpu:source_files.append(HERE/'build_cpu.py')
    sources={str(p.relative_to(NATIVE)) if p.is_relative_to(NATIVE) else '../'+str(p.relative_to(NATIVE.parent)):sha(p) for p in source_files}
    record={'target':target,'compiler':subprocess.check_output([compiler,'--version'],text=True).splitlines()[0],
            'sources':sources,'cpu_original_sha256':CPU[target],
            'files':{p.name:sha(p) for p in sorted(out.glob('*.so'))},
            'gpu_texture_units':2,'gpu_pvrtc1_formats':['0x8c00','0x8c01','0x8c02','0x8c03'],'startup_fallback_only':True,'original_cpu_unchanged':not material_cpu,
            'cpu_baseline_artifacts_preserved':True,'cpu_material_state_supported':material_cpu,'cpu_material_record':cpu_record}
    (out/'manifest.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--target',choices=['host','android-arm64-v8a','all'],default='all');p.add_argument('--selector-only',action='store_true');p.add_argument('--material-cpu',action='store_true',default=True);a=p.parse_args()
    for target in (['host','android-arm64-v8a'] if a.target=='all' else [a.target]):build(target,a.selector_only,a.material_cpu)
