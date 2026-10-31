"""Archive the no-motion public source matching the release APK frontend."""
from pathlib import Path
import hashlib,json,re,subprocess,tempfile,zipfile
H=Path(__file__).resolve().parent;R=H.parents[3];W=Path('/tmp/p2d-public-no-motion-20261101');O=R/'apk/1.0.3.1';COMMIT='0cc243a449fb2c2129b9b910afd4c934856482be'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
record=json.loads((O/'FINAL_VERIFICATION.json').read_text());assert record['passed'] and not record['gyro_tilt_included']
base=H/'build/current-public-source.zip';subprocess.run(['git','-C',str(W),'archive','--format=zip','--output='+str(base),COMMIT],check=True)
source=O/'Pocket2Droid-v1.0.3.1.Source.zip';inventory={};patched=[]
def adapt(name,data):
 if name=='android/AndroidManifest.xml':
  assert b'android:versionName="1.0.3-dev"' in data and b'android:versionCode="701"' in data
  data=data.replace(b'android:versionName="1.0.3-dev"',b'android:versionName="1.0.3.1"').replace(b'android:versionCode="701"',b'android:versionCode="703"')
 elif name=='android/build.py':
  assert b"VERSION = '1.0.3-dev'" in data and b'VERSION_CODE = 701' in data
  data=data.replace(b"VERSION = '1.0.3-dev'",b"VERSION = '1.0.3.1'").replace(b'VERSION_CODE = 701',b'VERSION_CODE = 703')
 elif name=='SOURCE_SNAPSHOT.json':
  d=json.loads(data);d.update(version='1.0.3.1',android_version_code=703,source_commit_before_release_metadata=COMMIT,apk_sha256=record['apk_sha256'],motion_support=False)
  d['not_an_exact_test19_apk_source']='Motion/tilt intentionally excluded; release APK uses the exact test17 native baseline and current ROM-update frontend.';data=(json.dumps(d,indent=2)+'\n').encode()
 elif name=='README.md':
  data=data.replace(b'## Current development source',b'## Current release: 1.0.3.1').replace(b'This is a development source snapshot, not a new APK or stable release.',b'This source archive accompanies the signed 1.0.3.1 release APK.')
 elif name=='BUILDING.md':
  data=data.replace(b'# Building the current development source',b'# Building the 1.0.3.1 release source').replace(b'This is 1.0.3 development source through test19 with motion/tilt excluded.',b'This is 1.0.3.1 release source through test19 with motion/tilt excluded.').replace(b'No APK is published by this source update.',b'The signed release APK is supplied separately from this source archive.').replace(b'This source-only variant is not identical to the signed test19 APK.',b'This release variant is not identical to the motion-enabled signed test19 APK.')
 return data
with zipfile.ZipFile(base) as src,zipfile.ZipFile(source,'x',zipfile.ZIP_DEFLATED,compresslevel=4) as z:
 for item in src.infolist():
  if item.is_dir():continue
  name=item.filename;raw=src.read(name);data=adapt(name,raw)
  if raw!=data:patched.append(name)
  assert not name.startswith('android/assets/motion-setup/') and not re.search(r'/Motion(?:Config|Math|Input|Feed|Setup)\.java$',name)
  assert not name.startswith('Key/')
  z.writestr(item,data);inventory[name]=hashlib.sha256(data).hexdigest()
 for name,p in [('release/build_release.py',H/'build_release.py'),('release/archive_release.py',Path(__file__)),('release/FINAL_VERIFICATION.json',O/'FINAL_VERIFICATION.json'),('release/signature.txt',O/'signature.txt'),('release/INSTALL.txt',O/'Pocket2Droid-v1.0.3.1-README.txt'),('release/FRONTEND-CHECKS.json',H/'FRONTEND-CHECKS.json'),('release/REVIEW.json',H/'REVIEW.json')]:
  data=p.read_bytes();z.writestr(name,data);inventory[name]=hashlib.sha256(data).hexdigest()
 z.writestr('SOURCE-SHA256.json',json.dumps(inventory,indent=2)+'\n')
assert sorted(patched)==sorted(['android/AndroidManifest.xml','android/build.py','SOURCE_SNAPSHOT.json','README.md','BUILDING.md']),patched
with zipfile.ZipFile(source) as z:
 assert z.testzip() is None and len(z.namelist())==len(set(z.namelist()))
 for name,h in inventory.items():assert hashlib.sha256(z.read(name)).hexdigest()==h,name
 actual={name[len('android/src/'):]:h for name,h in inventory.items() if name.startswith('android/src/') and name.endswith('.java')};assert actual==record['java_sources']
 snapshot=json.loads(z.read('SOURCE_SNAPSHOT.json'));assert snapshot['apk_sha256']==record['apk_sha256'] and snapshot['version']=='1.0.3.1' and snapshot['android_version_code']==703 and not snapshot['motion_support']
(O/'Pocket2Droid-v1.0.3.1-Release-Notes.txt').write_text('''Pocket2Droid v1.0.3.1

- Remove GLES support/renderer switches; graphics setup is now automatic with
  GPU preferred and the existing GLES1 CPU startup fallback.
- Normalize legacy Off/CPU profiles while retaining storage and other settings.
- Add a cyan GLES badge beneath the filename for recognized built-in GLES ROMs.
  Capability comes from ROM contents, not a name containing GLES.
- Retain gyro exclusion, Update/Change ROM and existing graphics compatibility.

Install as an update and keep app data. Existing1.0.3 GLES BINs remain compatible.
GLES2 requires a working GPU backend. The badge does not claim active GPU usage.
No new phone gameplay or FPS result is claimed. No games or Windows ROM images
are included in the APK or source ZIP.
''')

artifacts={p.name:dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size) for p in [O/'Pocket2Droid-v1.0.3.1.apk',source,O/'Pocket2Droid-v1.0.3.1-README.txt',O/'Pocket2Droid-v1.0.3.1-Release-Notes.txt']}
checksums=O/'Pocket2Droid-v1.0.3.1-SHA256SUMS.txt';checksums.write_text(''.join(v['sha256']+'  '+name+'\n' for name,v in artifacts.items()))
artifacts[checksums.name]=dict(path=str(checksums),sha256=sha(checksums),bytes=checksums.stat().st_size)
result=dict(passed=True,version='1.0.3.1',version_code=703,source_archive=source.name,source_sha256=sha(source),source_files=len(inventory),java_source_identity_verified=True,release_metadata_paths=patched,artifacts=artifacts,github_publication='User will publish',phone_installed=False)
(O/'DELIVERY.json').write_text(json.dumps(result,indent=2)+'\n');(H/'SOURCE-ARCHIVE.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='artifacts'},indent=2))
