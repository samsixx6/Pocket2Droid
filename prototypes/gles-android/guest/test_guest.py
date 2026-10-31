"""Exercise the same portable C scanner/ownership engine used in setup.exe."""
from pathlib import Path
import hashlib, json, os, struct, subprocess, tempfile, unittest

HERE = Path(__file__).resolve().parent
EXPERIMENT = HERE.parents[2]
CLI = HERE / 'build/host-test'

def image(module=b'libGLES_CL.dll', thunk=0x80000016, machine=0x1c0):
    b = bytearray(2048)
    b[:2] = b'MZ'; struct.pack_into('<I', b, 60, 128)
    b[128:132] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', b, 132, machine, 1, 0, 0, 0, 224, 0x102)
    struct.pack_into('<H', b, 152, 0x10b)
    struct.pack_into('<H', b, 220, 9)
    struct.pack_into('<I', b, 244, 16)
    struct.pack_into('<II', b, 256, 0x1000, 40)
    struct.pack_into('<8sIIIIIIHHI', b, 376, b'.idata', 1024, 0x1000, 1024, 512, 0, 0, 0, 0, 0x40000040)
    struct.pack_into('<IIIII', b, 512, 0x1080, 0, 0, 0x1040, 0x1080)
    b[576:576+len(module)+1] = module+b'\0'
    struct.pack_into('<II', b, 640, thunk, 0)
    if not thunk & 0x80000000:
        struct.pack_into('<H', b, 704, 0)
        b[706:721] = b'eglSwapBuffers\0'
    return bytes(b)

class GuestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.asset = self.root/'asset.dll'; self.asset.write_bytes(b'verified bridge asset')
        self.dest = self.root/'game'/'libGLES_CL.dll'; self.dest.parent.mkdir()
    def tearDown(self): self.tmp.cleanup()
    def cli(self, *args):
        p = subprocess.run([str(CLI), *map(str,args)], text=True, capture_output=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        return int(p.stdout.strip())
    def scan(self, content):
        p=self.root/'game.exe'; p.write_bytes(content)
        return self.cli('scan',p)
    def test_valid_cl_ordinal_and_cm_name_are_admitted(self):
        self.assertEqual(self.scan(image()),1)
        self.assertEqual(self.scan(image(b'libGLES_CM.dll',0x10c0)),2)
    def test_hash_records_use_standard_sha256(self):
        for data in [b'',b'abc',bytes(range(256))*33]:
            self.asset.write_bytes(data)
            p=subprocess.run([str(CLI),'sha',str(self.asset)],text=True,capture_output=True,check=True)
            self.assertEqual(p.stdout.strip(),hashlib.sha256(data).hexdigest())
    def test_second_live_session_rejected_and_owner_exit_allows_next(self):
        key=str(self.root/'session')
        owner=subprocess.Popen([str(CLI),'session',key,'hold'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        try:
            self.assertEqual(owner.stdout.readline().strip(),'1')
            self.assertEqual(self.cli('session',key),0)
            self.assertEqual(self.cli('session',key),0)
        finally:
            owner.communicate('x',timeout=5)
        self.assertEqual(self.cli('session',key),1)
    def test_abrupt_owner_death_does_not_leave_session_stuck(self):
        key=str(self.root/'crash-session')
        owner=subprocess.Popen([str(CLI),'session',key,'hold'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        try:
            self.assertEqual(owner.stdout.readline().strip(),'1')
            self.assertEqual(self.cli('session',key),0)
        finally:
            owner.kill();owner.communicate(timeout=5)
        self.assertEqual(self.cli('session',key),1)
    def test_shortcut_blob_is_owned_and_reversible(self):
        link=self.root/'game'/'owned.lnk'
        self.assertEqual(self.cli('blob',link),1)
        self.assertEqual(link.read_bytes(),b'literal shortcut')
        self.assertEqual(self.cli('off',link),1)
        self.assertFalse(link.exists())
    def test_readonly_asset_becomes_removable_ours_original_attrs_retained(self):
        self.asset.chmod(0o444)
        self.dest.write_bytes(b'original readonly DLL');self.dest.chmod(0o444)
        self.assertEqual(self.cli('on',self.dest,self.asset),1)
        self.assertNotEqual(self.dest.stat().st_mode&0o222,0)
        self.assertEqual(Path(str(self.dest)+'.p2d-gles-v1.backup').stat().st_mode&0o222,0)
        self.assertEqual(self.cli('off',self.dest),1)
        self.assertEqual(self.dest.read_bytes(),b'original readonly DLL')
        self.assertEqual(self.dest.stat().st_mode&0o222,0)
    def test_off_repairs_readonly_bit_only_on_verified_owned_bytes(self):
        self.dest.write_bytes(b'original');self.cli('on',self.dest,self.asset)
        self.dest.chmod(0o444)
        self.assertEqual(self.cli('off',self.dest),1)
        self.assertEqual(self.dest.read_bytes(),b'original')
        self.cli('on',self.dest,self.asset);self.dest.write_bytes(b'user changed');self.dest.chmod(0o444)
        self.assertEqual(self.cli('off',self.dest),-2)
        self.assertEqual(self.dest.read_bytes(),b'user changed')
        self.assertEqual(self.dest.stat().st_mode&0o222,0)
    def test_ordinary_pe_and_unsupported_api_are_not_admitted(self):
        self.assertEqual(self.scan(image(b'coredll.dll')),0)
        self.assertLess(self.scan(image(thunk=0x80000002)),0) # eglCopyBuffers not implemented
        self.assertLess(self.scan(image(thunk=0x8000ffff)),0)
        self.assertLess(self.scan(image(thunk=0x80000000)),0) # ordinal zero is never a valid bridge export
    def test_dll_masquerading_as_exe_is_not_a_game(self):
        b=bytearray(image());struct.pack_into('<H',b,150,0x2102)
        self.assertLess(self.scan(b),0)
    def test_malformed_or_non_ce_or_non_arm_is_rejected(self):
        self.assertLess(self.scan(image()[:600]),0)
        self.assertLess(self.scan(image(machine=0x14c)),0)
        b=bytearray(image());struct.pack_into('<H',b,220,2)
        self.assertLess(self.scan(b),0)
        b=bytearray(image());struct.pack_into('<I',b,512,0xfffffff0)
        self.assertLess(self.scan(b),0)
    def test_install_and_off_restore_original_bytes(self):
        self.dest.write_bytes(b'user original DLL')
        self.assertEqual(self.cli('on',self.dest,self.asset),1)
        self.assertEqual(self.dest.read_bytes(),b'verified bridge asset')
        self.assertEqual(self.cli('on',self.dest,self.asset),2)
        self.assertEqual(self.cli('off',self.dest),1)
        self.assertEqual(self.dest.read_bytes(),b'user original DLL')
        self.assertEqual(list(self.dest.parent.iterdir()),[self.dest])
    def test_off_removes_only_ours_and_preserves_user_edit(self):
        self.assertEqual(self.cli('on',self.dest,self.asset),1)
        self.assertEqual(self.cli('off',self.dest),1)
        self.assertFalse(self.dest.exists())
        self.dest.write_bytes(b'original')
        self.cli('on',self.dest,self.asset); self.dest.write_bytes(b'user edit after setup')
        self.assertEqual(self.cli('off',self.dest),-2)
        self.assertEqual(self.dest.read_bytes(),b'user edit after setup')
        self.assertEqual((Path(str(self.dest)+'.p2d-gles-v1.backup')).read_bytes(),b'original')
    def test_unrelated_backup_or_owner_collision_never_overwrites(self):
        self.dest.write_bytes(b'original')
        backup=Path(str(self.dest)+'.p2d-gles-v1.backup');backup.write_bytes(b'personal data')
        self.assertEqual(self.cli('on',self.dest,self.asset),-2)
        self.assertEqual(self.dest.read_bytes(),b'original')
        self.assertEqual(backup.read_bytes(),b'personal data')
        backup.unlink(); owner=Path(str(self.dest)+'.p2d-gles-v1.owner');owner.write_bytes(b'personal owner file')
        self.assertEqual(self.cli('on',self.dest,self.asset),-2)
        self.assertEqual(self.dest.read_bytes(),b'original')
        self.assertEqual(owner.read_bytes(),b'personal owner file')
    def test_identical_preexisting_dll_is_not_claimed_or_deleted(self):
        self.dest.write_bytes(self.asset.read_bytes())
        self.assertEqual(self.cli('on',self.dest,self.asset),3)
        self.assertEqual(self.cli('off',self.dest),0)
        self.assertTrue(self.dest.exists())
    def test_corrupt_backup_preserves_installed_dll(self):
        self.dest.write_bytes(b'original');self.cli('on',self.dest,self.asset)
        Path(str(self.dest)+'.p2d-gles-v1.backup').write_bytes(b'changed backup')
        self.assertEqual(self.cli('off',self.dest),-2)
        self.assertEqual(self.dest.read_bytes(),self.asset.read_bytes())
    def test_interrupted_after_backup_recovers_on_or_off(self):
        self.dest.write_bytes(b'original');self.cli('on',self.dest,self.asset)
        self.dest.unlink()
        self.assertEqual(self.cli('on',self.dest,self.asset),1)
        self.dest.unlink()
        self.assertEqual(self.cli('off',self.dest),1)
        self.assertEqual(self.dest.read_bytes(),b'original')
    def test_actual_cod2_variants_use_imports_not_filename(self):
        root=EXPERIMENT/'prototypes/gles-support/bridge-lab/runs/cod2-d258f340ad/card/COD2'
        if not root.exists(): self.skipTest('Recovered CoD2 fixture unavailable')
        self.assertEqual(self.cli('scan',root/'cod2_gles.exe'),1)
        self.assertEqual(self.cli('scan',root/'cod2.exe'),0)
        self.assertEqual(self.cli('scan',root/'cod2_goforce.exe'),2)
        renamed=self.root/'ordinary.exe';renamed.write_bytes((root/'cod2_gles.exe').read_bytes())
        self.assertEqual(self.cli('scan',renamed),1)

if __name__=='__main__': unittest.main(verbosity=2)
