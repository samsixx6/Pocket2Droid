"""Real Java/JNI filesystem profile tests; no APK or user guest data."""
from pathlib import Path
import argparse, os, shutil, subprocess, hashlib
HERE=Path(__file__).resolve().parents[1];SRC=HERE/'src/com/samsixx/pocket2droid'
p=argparse.ArgumentParser();p.add_argument('--repository',action='store_true');p.add_argument('--settings',action='store_true');p.add_argument('--controls',action='store_true');p.add_argument('--fallback',action='store_true');a=p.parse_args()
B=HERE/'build/library-tests';B.mkdir(parents=True,exist_ok=True)
api=Path('/opt/pocket2droid-android-tools/android-36/android.jar')
dep=B/'json.jar'
if not dep.exists():shutil.copyfile(HERE/'tests/dependencies/json-20240303.jar',dep)
assert hashlib.sha256(dep.read_bytes()).hexdigest()=='3cf6cd6892e32e2b4c1c39e0f52f5248a2f5b37646fdfbb79a66b46b618414ed','Host-only JSON dependency identity mismatch'
(B/'json.sha256').write_text(hashlib.sha256(dep.read_bytes()).hexdigest()+'\n')
for name in ('RomRepository.java','RomSettings.java','RomProfile.java','ProfileFiles.java','SessionGuard.java'):
 assert (SRC/name).exists(),'Missing profile implementation: '+name
jdk=Path(shutil.which('javac')).resolve().parents[1]
subprocess.run(['cc','-std=c11','-O2','-shared','-fPIC','-Wall','-Wextra','-Werror','-Dsyscall=p2d_test_syscall','-Dwrite=p2d_test_write','-Dfsync=p2d_test_fsync','-I'+str(jdk/'include'),'-I'+str(jdk/'include/linux'),str(HERE/'native/storage_publish.c'),str(HERE/'tests/storage_faults.c'),'-o',str(B/'libp2dstorage.so')],check=True)
names=('StorageLayout','StoragePublish','RomRepository','RomUpdate','GuestRam','RomSettings','RomProfile','ProfileFiles','SessionGuard','DisplayTransform','FrameProtocol','NetworkConfig')
sources=[SRC/(n+'.java') for n in names if (SRC/(n+'.java')).exists()]
if a.controls:sources.extend(SRC/(n+'.java') for n in ('ControlsLayout','ControlsState'))
tests=[HERE/'tests/RomRepositoryTest.java']
if a.settings:tests.append(HERE/'tests/RomSettingsTest.java')
if a.controls:tests.append(HERE/'tests/ControlsTest.java')
cp=str(B/'json.jar')+':'+str(api)
subprocess.run(['javac','--release','8','-encoding','UTF-8','-cp',cp,'-d',str(B),*map(str,sources+tests)],check=True)
env=dict(os.environ)
if a.fallback:env['P2D_TEST_STORAGE_UNSUPPORTED']='1'
subprocess.run(['java','-Djava.library.path='+str(B),'-cp',str(B)+':'+cp,'com.samsixx.pocket2droid.RomRepositoryTest'],env=env,check=True)
if a.settings:subprocess.run(['java','-cp',str(B)+':'+cp,'com.samsixx.pocket2droid.RomSettingsTest'],check=True)
if a.controls:subprocess.run(['java','-cp',str(B)+':'+cp,'com.samsixx.pocket2droid.ControlsTest'],check=True)
