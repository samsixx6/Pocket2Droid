"""Build shared GLES helpers/CPU adapter, preserving sealed release artifacts."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
HERE=Path(__file__).resolve().parent;NATIVE=HERE.parent;GLES=NATIVE.parent
HOST=GLES.parent/'gles-support/bridge-lab/host';TOOLS=HOST.parent/'tools'
NDK=Path('/opt/pocket2droid-android-tools/bionic/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64/bin')
BASELINE={'host':'2ef8841407ff40a293426f539da6bc418d946d8b7434355ffaa8f0e53d5c6dbc','android-arm64-v8a':'7972c8fbbf27d6da719b89360107aeefc06ae1e216befa20e13ca63b6ade6d9c'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def inputs():
    paths=[NATIVE/'Cargo.toml',NATIVE/'Cargo.lock',Path(__file__),HOST/'Cargo.toml',HOST/'Cargo.lock']
    for root in (NATIVE/'src',HOST/'src',HOST/'vendor'):
        paths.extend(p for p in root.rglob('*') if p.is_file() and 'target' not in p.parts)
    project=GLES.parents[3]
    return {str(p.relative_to(project)):sha(p) for p in sorted(set(paths))}
def main():
    for target,h in BASELINE.items():assert sha(NATIVE/'artifacts'/target/'libp2d_gles.so')==h
    before=inputs();out=HERE/'cpu-build';out.mkdir(exist_ok=True)
    env=dict(os.environ);env.update(RUSTUP_HOME=str(TOOLS/'rustup'),CARGO_HOME=str(TOOLS/'cargo'),CARGO_TARGET_DIR=str(NATIVE/'target'))
    env['PATH']=str(TOOLS/'cargo/bin')+os.pathsep+env.get('PATH','');env.pop('POCKETHLE_DUMP_TEXTURES',None)
    cargo=str(TOOLS/'cargo/bin/cargo')
    for target in ('host','android-arm64-v8a'):
        selected=dict(env);command=[cargo,'build','--release','--locked','--offline']
        if target!='host':
            command+=['--target','aarch64-linux-android']
            selected['CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER']=str(NDK/'aarch64-linux-android24-clang')
            selected['CARGO_TARGET_AARCH64_LINUX_ANDROID_RUSTFLAGS']='-C link-arg=-Wl,-z,max-page-size=16384 -C link-arg=-Wl,-soname,libp2d_gles_cpu.so'
        with (out/(target+'.log')).open('wb') as log:subprocess.run(command,cwd=NATIVE,env=selected,stdout=log,stderr=subprocess.STDOUT,check=True)
        source=NATIVE/'target'/('release' if target=='host' else 'aarch64-linux-android/release')/'libp2d_gles.so'
        dest=out/target/'libp2d_gles_cpu.so';dest.parent.mkdir(exist_ok=True);shutil.copyfile(source,dest)
    assert before==inputs(),'CPU source changed during build'
    for target,h in BASELINE.items():assert sha(NATIVE/'artifacts'/target/'libp2d_gles.so')==h
    record={'sources':before,'baseline_artifacts_preserved':BASELINE,'files':{target:sha(out/target/'libp2d_gles_cpu.so') for target in BASELINE},'scope':'GLES adapter, shared texture decoding and weighted palette transforms; preserved sealed release artifacts'}
    (out/'manifest.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({'files':record['files'],'source_count':len(before)},indent=2))
if __name__=='__main__':main()
