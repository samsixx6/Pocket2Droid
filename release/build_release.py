"""Build the authorized public 1.0.3 APK without experimental motion support."""
from pathlib import Path
import hashlib,importlib.util,json,re,shutil,struct,subprocess,tempfile,time,zipfile
H=Path(__file__).resolve().parent;R=H.parents[3];W=Path('/tmp/p2d-public-no-motion-20261101');U=R/'GITHUB_PREVIEW/update-20261101-no-motion';SDK=Path('/opt/pocket2droid-android-tools/android-16')
VERSION='1.0.3.1';CODE=703;COMMIT='0cc243a449fb2c2129b9b910afd4c934856482be';SIGNER='85d6f87a557def48710b8e295ed58513888a99852843a90106b3b7f18e39681b';started=time.monotonic()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert json.loads((U/'AUDIT.json').read_text())['passed']
assert json.loads((U/'SHADER-REVIEW.json').read_text())['passed']
assert json.loads((U/'SHADER-PIXELS.json').read_text())['passed']
for case in ('guest1','guest2','network'):assert json.loads((U/(case+'.json')).read_text())['passed'],case
baseline=R/'apk/1.0.3-test17/Pocket2Droid-v1.0.3-test17-Siege-Renderer-Fix.apk';assert sha(baseline)=='bff8d66a8f4dfdf881584cbfc16e145eaeb59a10730411bcdd22931242edb17e'
J=W/'android/src';sources=sorted(J.rglob('*.java'));assert len(sources)==49
actual={p.relative_to(J).as_posix():sha(p) for p in sources}
assert json.loads((H/'FRONTEND-CHECKS.json').read_text())['java_sources']==actual
assert json.loads((H/'REVIEW.json').read_text())['passed']
for p in sources:
 assert not re.search(r'MotionConfig|MotionMath|MotionInput|MotionFeed|MotionSetup|TYPE_GRAVITY|TYPE_ACCELEROMETER|Calibrate tilt|Tilt steering',p.read_text()),p
# Public source commit and compiled source must agree exactly.
G=['git','-c','core.filemode=true','-c','core.symlinks=true','-C',str(W)]
for p in sources:assert subprocess.check_output(G+['show',COMMIT+':android/src/'+p.relative_to(J).as_posix()])==p.read_bytes(),p
out=R/'apk'/VERSION
if out.exists():assert not any(out.iterdir()),'Preserve an existing release'
else:out.mkdir()
work=H/'build';work.mkdir(exist_ok=True);attempt=Path(tempfile.mkdtemp(prefix='release103-',dir=work));classes=attempt/'classes';classes.mkdir();api=Path('/opt/pocket2droid-android-tools/android-36/android.jar')
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),capture_output=True,text=True)
 with (attempt/'build.log').open('a') as log:log.write(p.stdout+p.stderr)
 assert p.returncode==0,p.stderr[-3000:]
run(['javac','--release','8','-encoding','UTF-8','-cp',api,'-d',classes,*sources]);run(['jar','cf',attempt/'input.jar','-C',classes,'.']);run(['java','-cp',SDK/'lib/d8.jar','com.android.tools.r8.D8','--release','--min-api','24','--lib',api,'--output',attempt,attempt/'input.jar'])
replacements={'classes.dex':(attempt/'classes.dex').read_bytes()}
spec=importlib.util.spec_from_file_location('release_axml',R/'SOURCE/review-v640/Pocket2Droid-V6.40-Clean-Source/verify_signed_apk.py');axml=importlib.util.module_from_spec(spec);spec.loader.exec_module(axml)
def release_manifest(data):
 chunks=[];offset=8;patched=0;strings=None
 while offset<len(data):
  kind,hs,size=struct.unpack_from('<HHI',data,offset);assert size>=hs and offset+size<=len(data);chunk=bytearray(data[offset:offset+size])
  if kind==1:
   assert hs==28 and axml.u32(chunk,12)==0 and axml.u32(chunk,16)==0
   strings=axml.parse_string_pool(chunk);assert strings.count('1.0.3-test17')==1
   strings=[VERSION if s=='1.0.3-test17' else s for s in strings]
   encoded=bytearray();offsets=[]
   for s in strings:
    raw=s.encode('utf-16le');length=len(raw)//2;assert length<32768;offsets.append(len(encoded));encoded+=struct.pack('<H',length)+raw+b'\0\0'
   encoded+=bytes((-len(encoded))%4);start=28+len(strings)*4
   chunk=bytearray(struct.pack('<HH6I',1,28,start+len(encoded),len(strings),0,0,start,0)+struct.pack('<'+str(len(strings))+'I',*offsets)+encoded)
   assert axml.parse_string_pool(chunk)==strings
  elif kind==0x102 and strings[axml.u32(chunk,20)]=='manifest':
   base=16+axml.u16(chunk,24);stride=axml.u16(chunk,26)
   for index in range(axml.u16(chunk,28)):
    at=base+index*stride
    if strings[axml.u32(chunk,at+4)]=='versionCode':
     assert chunk[at+15]==0x10 and axml.u32(chunk,at+16)==699
     struct.pack_into('<I',chunk,at+16,CODE);patched+=1
  chunks.append(bytes(chunk));offset+=size
 assert patched==1 and offset==len(data)
 body=b''.join(chunks);return struct.pack('<HHI',3,8,8+len(body))+body
unsigned=attempt/'unsigned.apk';aligned=attempt/'aligned.apk';signed=attempt/'Pocket2Droid-v1.0.3.1.apk'
with zipfile.ZipFile(baseline) as old:
 replacements['AndroidManifest.xml']=release_manifest(old.read('AndroidManifest.xml'))
 before=axml.parse_manifest(old.read('AndroidManifest.xml'));after=axml.parse_manifest(replacements['AndroidManifest.xml']);assert len(before)==len(after)
 for (tag,a),(tag2,b) in zip(before,after):
  assert tag==tag2
  if tag=='manifest':a=dict(a,versionName=VERSION,versionCode=CODE)
  assert a==b,(tag,a,b)
 with zipfile.ZipFile(unsigned,'x') as z:
  for item in old.infolist():
   if item.filename.startswith('META-INF/'):continue
   entry=zipfile.ZipInfo(item.filename,item.date_time);entry.external_attr=item.external_attr;entry.compress_type=item.compress_type;z.writestr(entry,replacements.get(item.filename,old.read(item.filename)))
run([SDK/'zipalign','-P','16','4',unsigned,aligned])
key=R/'Key/wm6-v2-development.p12';match=re.search(r'(?im)\bpassword\s*[:=]\s*([^\r\n]+)',(key.parent/'README.txt').read_text());assert match
secret=None
try:
 with tempfile.NamedTemporaryFile(mode='w',prefix='.signpass-',dir=attempt,delete=False) as f:
  f.write(match.group(1).strip().strip(chr(96)+chr(34)+chr(39))+'\n');secret=Path(f.name)
 run(['java','-jar',SDK/'lib/apksigner.jar','sign','--ks',key,'--ks-type','PKCS12','--ks-pass','file:'+str(secret),'--out',signed,aligned])
finally:
 if secret:secret.unlink(missing_ok=True)
signature=subprocess.check_output(['java','-jar',str(SDK/'lib/apksigner.jar'),'verify','--verbose','--print-certs',str(signed)],text=True);assert SIGNER in signature
assert SIGNER in subprocess.check_output(['java','-jar',str(SDK/'lib/apksigner.jar'),'verify','--print-certs',str(baseline)],text=True)
run([SDK/'zipalign','-c','-P','16','4',signed])
# Independently verify v2 certificate signature and complete content digest.
raw=signed.read_bytes();eocd,cdsize,cdoff=axml.find_eocd(raw);ss,se=axml.locate_signature_block(raw,cdoff);da,expected,sa,sig,cert,sd=axml.parse_v2_signature(raw,ss,se)
assert da==sa==0x103 and expected==axml.calculate_content_digest(raw,ss,cdoff,eocd)
certificate=axml.x509.load_der_x509_certificate(cert);certificate.public_key().verify(sig,sd,axml.padding.PKCS1v15(),axml.hashes.SHA256());assert hashlib.sha256(cert).hexdigest()==SIGNER
changed=[];entries={};libraries={};manifest={}
with zipfile.ZipFile(signed) as z,zipfile.ZipFile(baseline) as old:
 assert z.testzip() is None and len(z.namelist())==len(set(z.namelist())) and set(z.namelist())==set(old.namelist())
 for name in z.namelist():
  data=z.read(name);entries[name]=hashlib.sha256(data).hexdigest()
  assert not name.startswith('assets/motion-setup/')
  if data!=old.read(name):changed.append(name)
  if name not in replacements and not name.startswith('META-INF/'):assert data==old.read(name),name
  if name in replacements:assert data==replacements[name],name
  if name.startswith('lib/'):
   item=z.getinfo(name);assert item.compress_type==0 and data[:4]==b'\x7fELF' and struct.unpack_from('<H',data,18)[0]==183
   with signed.open('rb') as f:f.seek(item.header_offset);header=f.read(30)
   assert (item.header_offset+30+sum(struct.unpack_from('<HH',header,26)))%16384==0
   ph=struct.unpack_from('<Q',data,32)[0];size,count=struct.unpack_from('<HH',data,54)
   for i in range(count):
    at=ph+i*size
    if struct.unpack_from('<I',data,at)[0]==1:
     offset,vaddr=struct.unpack_from('<QQ',data,at+8);alignment=struct.unpack_from('<Q',data,at+48)[0];assert alignment>=16384 and (offset-vaddr)%alignment==0
   libraries[name]=entries[name]
 for group in ['gles-setup','network-setup']:
  asset_manifest=json.loads(z.read('assets/'+group+'/manifest.json'))
  if group=='gles-setup':
   for name,digest in asset_manifest['files'].items():assert hashlib.sha256(z.read('assets/'+group+'/'+name)).hexdigest()==digest
  else:assert hashlib.sha256(z.read('assets/network-setup/autorun.exe')).hexdigest()==asset_manifest['executable_sha256']
 dex=z.read('classes.dex')
 for name in ['MotionConfig','MotionMath','MotionInput','MotionFeed','MotionSetup','Calibrate tilt','Tilt steering']:assert name.encode() not in dex,name
 for tag,attrs in axml.parse_manifest(z.read('AndroidManifest.xml')):manifest.setdefault(tag,[]).append(attrs)
 assert manifest['manifest'][0]['versionName']==VERSION and manifest['manifest'][0]['versionCode']==CODE and manifest['manifest'][0]['package']=='com.samsixx.pocket2droid'
 assert not manifest['application'][0].get('testOnly',0) and not manifest['application'][0].get('debuggable',0)
 assert 'instrumentation' not in manifest
assert set(changed)==set(replacements)|{'META-INF/WM6-V2-D.SF','META-INF/WM6-V2-D.RSA','META-INF/MANIFEST.MF'}
# aapt validates the rebuilt binary manifest through the platform tooling too.
badging=subprocess.check_output([str(SDK/'aapt2'),'dump','badging',str(signed)],text=True);assert "versionCode='703'" in badging and "versionName='1.0.3.1'" in badging
(out/'badging.txt').write_text(badging);(out/'signature.txt').write_text(signature);dest=out/signed.name
with signed.open('rb') as a,dest.open('xb') as b:shutil.copyfileobj(a,b)
assert sha(dest)==sha(signed);shutil.copyfile(attempt/'build.log',out/'build.log')
record=dict(passed=True,version=VERSION,version_code=CODE,package='com.samsixx.pocket2droid',apk=str(dest),apk_sha256=sha(dest),signer=SIGNER,signature_verified=True,independent_v2_signature_and_digest_verified=True,alignment_16k=True,debuggable=False,test_only=False,gyro_tilt_included=False,automatic_gles=True,manual_graphics_controls=False,gles_badge='GLES',motion_asset_entries=0,java_source_commit=COMMIT,java_sources=actual,baseline_apk_sha256=sha(baseline),all_native_and_assets_identical_to_test17=True,libraries=libraries,entries=entries,changed_entries=changed,phone_actions=False,new_gameplay_validation=False,seconds=time.monotonic()-started)
(out/'FINAL_VERIFICATION.json').write_text(json.dumps(record,indent=2)+'\n');(H/'APK-BUILD.json').write_text(json.dumps(record,indent=2)+'\n')
(out/'Pocket2Droid-v1.0.3.1-README.txt').write_text('''Pocket2Droid 1.0.3.1 (code703) - corrected public release

Close the ROM normally, then install this APK as an update. The official
package and signing key are retained. Keep existing app data.

GLES is automatic and GPU preferred. The old GLES support and renderer options
are removed. Valid legacy Off/CPU selections migrate automatically; other ROM
settings and storage are retained. GLES1 retains automatic CPU fallback when
GPU initialization is unavailable; GLES2 still requires a working GPU backend.

A cyan GLES badge appears below the filename for recognized raw ROMs with
built-in ARM CE GLES bridge DLL files. Renaming a plain ROM to include GLES
cannot create the badge. Detection is read-only and occurs off the UI thread.
The badge means built-in support, not active GPU usage or universal compatibility.
Compressed and unrelated physical-device ROM layouts are not detected.

Gyro/tilt remains excluded. Update/Change ROM is retained. Existing v1.0.3
GLES BINs and graphics DLLs remain compatible; no replacement BIN is required.
Older instructions to select GLES On/GPU preferred no longer apply: automatic
setup now supplies the verified assets and host graphics endpoint.

The pre-motion test17 native engine, all graphics/storage libraries and assets
are byte-identical. Signature/content identity and16KiB APK/ELF alignment are
verified. Focused real profile, ROM-content, launch and PC Android UI checks pass.
No new phone/gameplay/performance validation is claimed for this UI correction.
Matching source: Pocket2Droid-v1.0.3.1.Source.zip.
''')

print(json.dumps({k:v for k,v in record.items() if k in ['passed','version','version_code','apk','apk_sha256','signature_verified','alignment_16k','gyro_tilt_included','all_native_and_assets_identical_to_test17','changed_entries','seconds']},indent=2))
