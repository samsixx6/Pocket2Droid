"""Game-free regression for the opaque shader SHA-256 recognizer."""
from pathlib import Path
import ctypes,hashlib,random,subprocess,tempfile,unittest
G=Path(__file__).resolve().parents[1]/'guest'
class Fingerprints(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();d=Path(cls.tmp.name)
  (d/'shim.c').write_text('#include <stdint.h>\ntypedef uint32_t U32;typedef int32_t I32;\nenum {P2D2_glGetShaderiv=1,P2D2_glShaderSource=2,P2D2_glCompileShader=3};\nstatic void fail(U32 e){(void)e;}\nstatic int query(U32 op,U32*a,const void*p,U32 n,void*out,U32 cap){(void)op;(void)a;(void)p;(void)n;(void)out;(void)cap;return 0;}\nstatic int record(U32 op,U32*a,const void*p,U32 n){(void)op;(void)a;(void)p;(void)n;return 0;}\n#include "east_shader_map.h"\nvoid hash(const unsigned char*p,U32 n,unsigned char*out){p2d_shader_sha256(p,n,out);}\nint recognizes(const unsigned char*p,U32 n){return east_shader_find(p,n)!=0;}\n')
  subprocess.run(['clang-17','-shared','-fPIC','-O2','-Werror','-I'+str(G),str(d/'shim.c'),'-o',str(d/'shim.so')],check=True)
  cls.lib=ctypes.CDLL(str(d/'shim.so'));cls.lib.hash.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p];cls.lib.recognizes.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_standard_and_padding_boundaries(self):
  rng=random.Random(156);samples=[b'',b'abc',b'a'*1000000]+[rng.randbytes(n) for n in range(130)]+[rng.randbytes(n) for n in (176,280,536,620,4096)]
  for value in samples:
   with self.subTest(size=len(value)):
    result=ctypes.create_string_buffer(32);self.lib.hash(value,len(value),result);self.assertEqual(result.raw,hashlib.sha256(value).digest())
 def test_unknown_binaries_are_rejected(self):
  for n in (0,1,175,176,280,536,620,621):self.assertEqual(self.lib.recognizes(bytes(n),n),0)
 def test_no_extracted_shader_payload_arrays(self):
  text=(G/'east_shader_map.h').read_text();self.assertNotRegex(text,r'const unsigned char east_\w+\[\]')
if __name__=='__main__':unittest.main()
