from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,time,tempfile
H=Path(__file__).resolve().parents[1];A=H;SRC=H/'src'
p=argparse.ArgumentParser();p.add_argument('--repository',action='store_true');p.add_argument('--case',default='basic');p.add_argument('--fallback',action='store_true');args=p.parse_args()
out=H/'build/tests'/(('repository' if args.repository else args.case)+('-fallback' if args.fallback else ''));out.mkdir(parents=True,exist_ok=True)
cp=str(A/'tests/dependencies/json-20240303.jar')+':/opt/pocket2droid-android-tools/android-36/android.jar';jdk=Path(shutil.which('javac')).resolve().parents[1]
def run(cmd,name,env=None):
 with (out/(name+'.log')).open('w') as f:r=subprocess.run(list(map(str,cmd)),stdout=f,stderr=subprocess.STDOUT,env=env)
 text=(out/(name+'.log')).read_text();print(text[-5000:],flush=True)
 if r.returncode:raise SystemExit(r.returncode)
run(['cc','-std=c11','-O2','-shared','-fPIC','-Wall','-Wextra','-Werror','-Dsyscall=p2d_test_syscall','-Dwrite=p2d_test_write','-Dfsync=p2d_test_fsync','-I'+str(jdk/'include'),'-I'+str(jdk/'include/linux'),A/'native/storage_publish.c',H/'tests/rom_update_faults.c','-o',out/'libp2dstorage.so'],'jni')
run(['javac','--release','8','-encoding','UTF-8','-cp',cp,'-d',out,*SRC.rglob('*.java'),H/'tests/RomUpdateTest.java',H/'tests/RomDeviceStateTest.java',A/'tests/RomRepositoryTest.java'],'compile')
jni=Path(tempfile.mkdtemp(prefix="p2d-rom-update-jni-"));shutil.copy2(out/"libp2dstorage.so",jni/"libp2dstorage.so");env=dict(os.environ);env["LD_PRELOAD"]=str(jni/"libp2dstorage.so")
if args.fallback:env['P2D_TEST_STORAGE_UNSUPPORTED']='1'
cases=['basic','reject','wm5','wm5-invalid','recovery','faults','partial','copy-failure','crashes','checkpoint-sync','file-sync','owner','completed','fresh-key','stale','device','fence','invalid-key'] if args.repository else args.case.split(',');started=time.monotonic()
for case in cases:run(['java','-Djava.library.path='+str(jni),'-cp',str(out)+':'+cp,'com.samsixx.pocket2droid.'+('RomDeviceStateTest' if case in ('device','fence','invalid-key') else 'RomUpdateTest'),case],case+('-fallback' if args.fallback else ''),env)
if args.repository:run(['java','-Djava.library.path='+str(out),'-cp',str(out)+':'+cp,'com.samsixx.pocket2droid.RomRepositoryTest'],'existing'+('-fallback' if args.fallback else ''),env)
(out/('RESULT-FALLBACK.json' if args.fallback else 'RESULT.json')).write_text(json.dumps(dict(passed=True,cases=cases,existing_repository=args.repository,elapsed_seconds=time.monotonic()-started),indent=2)+'\n')
