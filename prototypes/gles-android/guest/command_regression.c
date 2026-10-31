/* Included after the actual bridge, before portable WinCE fixture definitions. */
#define CreateFileW fixture_base_create
#define ReadFile fixture_base_read
#define WriteFile fixture_base_write
#define main capability_main
#include "capability_regression.c"
#undef main
#undef CreateFileW
#undef ReadFile
#undef WriteFile

static U32 fixture_host_error, fixture_response_offset, fixture_write_failure;
H CreateFileW(const W *p,U32 access,U32 share,void *security,U32 disposition,U32 flags,H template){
 if(wide_equal(p,"\\Storage Card\\P2DGLES\\mode.txt")){mode_missing=0;if(!mode_size){memcpy(mode_data,"on gpu",6);mode_size=6;}}
 if(wide_equal(p,"\\Storage Card\\P2DGLES\\request.bin"))return(H)20;
 if(wide_equal(p,"\\Storage Card\\P2DGLES\\request.ready"))return(H)21;
 if(wide_equal(p,"\\Storage Card\\P2DGLES\\response.bin")){fixture_response_offset=0;return(H)22;}
 return fixture_base_create(p,access,share,security,disposition,flags,template);
}
int WriteFile(H h,const void*p,U32 n,U32*done,void*o){
 if(h==(H)20||h==(H)21){*done=fixture_write_failure?0:n;return !fixture_write_failure;}
 return fixture_base_write(h,p,n,done,o);
}
int ReadFile(H h,void*p,U32 n,U32*done,void*o){
 if(h!=(H)22)return fixture_base_read(h,p,n,done,o);
 if(!fixture_response_offset){U32 h[]={0x31524750,sequence,width,height,width*height*2,fixture_host_error};assert(n==24);memcpy(p,h,24);}
 else memset(p,0,n);
 *done=n;fixture_response_offset+=n;return 1;
}
static void command(U32 op,U32 value){U32 r[4]={value,0,0,0};bridge(op,r,0,1);}
static void exactly(U32 op,U32 value,U32 emitted){U32 before=records;command(op,value);assert(records==before+emitted);}
static void repeat(U32 op,U32 value){
 exactly(op,value,1);U32 before=records;command(op,value);
 assert(records==before&&"repeated valid state must avoid argument packing and packet emission");
}
static void test_repeat(void){
 repeat(OP_glActiveTexture,0x84c0);repeat(OP_glClientActiveTexture,0x84c0);
 repeat(OP_glMatrixMode,0x1700);repeat(OP_glEnableClientState,0x8074);
 repeat(OP_glDisableClientState,0x8075);repeat(OP_glEnable,0xde1);
 exactly(OP_glDisable,0xde1,1);exactly(OP_glDisable,0xde1,0);
 exactly(OP_glDisableClientState,0x8074,1);exactly(OP_glDisableClientState,0x8074,0);
}
static void test_units(void){
 command(OP_glActiveTexture,0x84c0);repeat(OP_glEnable,0xde1);
 command(OP_glActiveTexture,0x84c1);repeat(OP_glDisable,0xde1);
 command(OP_glActiveTexture,0x84c0);exactly(OP_glEnable,0xde1,0);
 command(OP_glClientActiveTexture,0x84c0);repeat(OP_glEnableClientState,0x8078);
 command(OP_glClientActiveTexture,0x84c1);repeat(OP_glDisableClientState,0x8078);
 command(OP_glClientActiveTexture,0x84c0);exactly(OP_glEnableClientState,0x8078,0);
 /* Vertex/color/normal enable bits are not texture-unit state. */
 repeat(OP_glEnableClientState,0x8076);command(OP_glClientActiveTexture,0x84c1);
 exactly(OP_glEnableClientState,0x8076,0);
}
static void test_invalid(void){
 command(OP_glActiveTexture,0x84c0);U32 before=records;
 command(OP_glActiveTexture,0x84c2);assert(glerror==0x500&&records==before&&server_unit==0);glerror=0;
 command(OP_glActiveTexture,0x84c2);assert(glerror==0x500&&records==before);glerror=0;
 command(OP_glClientActiveTexture,0x84bf);assert(glerror==0x500&&client_unit==0);glerror=0;
 /* Unrecognized enums must still reach the renderer each time. */
 exactly(OP_glMatrixMode,0xbad,1);exactly(OP_glMatrixMode,0xbad,1);
 exactly(OP_glEnableClientState,0xbad,1);exactly(OP_glEnableClientState,0xbad,1);
 exactly(OP_glEnable,0xbad,1);exactly(OP_glEnable,0xbad,1);
 exactly(OP_glEnable,0xbe2,1);exactly(OP_glEnable,0xbe2,1);
 exactly(OP_glMatrixMode,0x1700,1);exactly(OP_glMatrixMode,0x1700,0);
}
static void flush_fixture(void){U32 r[4]={0};assert(bridge(OP_eglSwapBuffers,r,0,0)==1);assert(!records&&!failed);}
static void test_boundaries(void){
 repeat(OP_glActiveTexture,0x84c1);repeat(OP_glClientActiveTexture,0x84c1);
 repeat(OP_glEnable,0xde1);repeat(OP_glEnableClientState,0x8078);
 flush_fixture();exactly(OP_glActiveTexture,0x84c1,0);exactly(OP_glEnable,0xde1,0);
 exactly(OP_glClientActiveTexture,0x84c1,0);exactly(OP_glEnableClientState,0x8078,0);
 command(OP_glMatrixMode,0x1701);fixture_host_error=0x502;flush_fixture();assert(glerror==0x502);glerror=0;
 /* A rejected packet cannot certify any earlier cached state. Unit-dependent
    calls before a fresh selector must not establish a false per-unit proof. */
 exactly(OP_glEnable,0xde1,1);exactly(OP_glEnableClientState,0x8078,1);
 exactly(OP_glActiveTexture,0x84c1,1);exactly(OP_glClientActiveTexture,0x84c1,1);
 exactly(OP_glEnable,0xde1,1);exactly(OP_glEnableClientState,0x8078,1);
 exactly(OP_glEnable,0xde1,0);exactly(OP_glEnableClientState,0x8078,0);
 fixture_host_error=0;flush_fixture();
 command(OP_eglDestroyContext,3);exactly(OP_glActiveTexture,0x84c1,1);
 command(OP_eglCreateContext,0);exactly(OP_glActiveTexture,0x84c1,1);
 command(OP_eglMakeCurrent,0);exactly(OP_glActiveTexture,0x84c1,1);
 command(OP_eglMakeCurrent,1);exactly(OP_glActiveTexture,0x84c1,1);
 command(OP_eglTerminate,1);exactly(OP_glActiveTexture,0x84c1,1);
}
static void test_failed_record(void){
 repeat(OP_glActiveTexture,0x84c0);used=MAX_PACKET;fixture_write_failure=1;
 command(OP_glActiveTexture,0x84c1);assert(failed&&used==MAX_PACKET);
 /* A fixture-only recovery tests that failed insertion did not commit a proof.
    Production failures stay sticky and init blocks further guest commands. */
 failed=0;used=24;records=0;fixture_write_failure=0;
 exactly(OP_glActiveTexture,0x84c1,1);exactly(OP_glActiveTexture,0x84c1,0);
}
static void test_capacity_error(void){
 command(OP_glActiveTexture,0x84c1);command(OP_glClientActiveTexture,0x84c1);
 command(OP_glEnable,0xde1);command(OP_glEnableClientState,0x8078);
 used=MAX_PACKET;fixture_host_error=0x502;
 command(OP_glDisable,0xde1);assert(records==1&&used==96&&glerror==0x502&&!failed);
 fixture_host_error=0;glerror=0;
 /* The capacity flush invalidated selectors before disable was inserted. */
 exactly(OP_glDisable,0xde1,1);exactly(OP_glDisable,0xde1,1);
 exactly(OP_glActiveTexture,0x84c1,1);exactly(OP_glDisable,0xde1,1);exactly(OP_glDisable,0xde1,0);
 exactly(OP_glClientActiveTexture,0x84c1,1);repeat(OP_glDisableClientState,0x8078);
}
static void test_four_units(void){
 assert(texture_units==4);
 for(U32 unit=0;unit<4;unit++){
  command(OP_glActiveTexture,0x84c0+unit);repeat(OP_glEnable,0xde1);
  command(OP_glClientActiveTexture,0x84c0+unit);repeat(OP_glEnableClientState,0x8078);
 }
 for(U32 unit=0;unit<4;unit++){
  command(OP_glActiveTexture,0x84c0+unit);exactly(OP_glEnable,0xde1,0);
  command(OP_glClientActiveTexture,0x84c0+unit);exactly(OP_glEnableClientState,0x8078,0);
 }
 exactly(OP_glDisable,0xde1,1);command(OP_glActiveTexture,0x84c0);exactly(OP_glEnable,0xde1,0);
}
static void test_bulk(void){
 unsigned char src[256],dst[256],expected[256];
 for(unsigned i=0;i<sizeof(src);i++)src[i]=(unsigned char)(i*37+9);
 for(unsigned s=0;s<8;s++)for(unsigned d=0;d<8;d++)for(unsigned n=0;n<=130;n++){
  memset(dst,0xa5,sizeof(dst));memcpy(expected,dst,sizeof(dst));memcpy(expected+d,src+s,n);
  bytes(dst+d,src+s,n);assert(!memcmp(dst,expected,sizeof(dst)));
  memset(dst,0xa5,sizeof(dst));memcpy(expected,dst,sizeof(dst));memset(expected+d,0,n);
  zero(dst+d,n);assert(!memcmp(dst,expected,sizeof(dst)));
 }
 for(unsigned s=0;s<32;s++)for(unsigned d=0;d<32;d++){
  memcpy(dst,src,sizeof(dst));memcpy(expected,src,sizeof(dst));
  for(unsigned i=0;i<100;i++)expected[d+i]=expected[s+i];
  bytes(dst+d,dst+s,100);assert(!memcmp(dst,expected,sizeof(dst)));
 }
 unsigned char *a=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 unsigned char *b=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(a!=MAP_FAILED&&b!=MAP_FAILED);assert(!mprotect(a+4096,4096,PROT_NONE)&&!mprotect(b+4096,4096,PROT_NONE));
 for(unsigned n=0;n<130;n++){memset(a+4096-n,0x37,n);bytes(b+4096-n,a+4096-n,n);assert(!memcmp(a+4096-n,b+4096-n,n));zero(b+4096-n,n);}
 munmap(a,8192);munmap(b,8192);
}
int main(int argc,char**argv){
 assert(argc==2);mode_missing=1;
 if(!strcmp(argv[1],"four-units")){mode_missing=0;memcpy(mode_data,"on cpu",6);mode_size=6;}
 assert(init());
 if(!strcmp(argv[1],"repeat"))test_repeat();else if(!strcmp(argv[1],"units"))test_units();
 else if(!strcmp(argv[1],"invalid"))test_invalid();else if(!strcmp(argv[1],"boundaries"))test_boundaries();
 else if(!strcmp(argv[1],"failed-record"))test_failed_record();else if(!strcmp(argv[1],"capacity-error"))test_capacity_error();
 else if(!strcmp(argv[1],"four-units"))test_four_units();else if(!strcmp(argv[1],"bulk"))test_bulk();else abort();
 puts("PASS actual guest command packing/state/error/bulk regression");return 0;
}
