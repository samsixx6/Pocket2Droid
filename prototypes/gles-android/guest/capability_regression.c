/* Exercise the actual bridge through a 32-bit guest ABI and WinCE file calls. */
static unsigned char mode_data[64];
static U32 mode_size,mode_offset,mode_chunk,fail_read,mode_opens,mode_reads,mode_closes,transport_writes,session_claims;
static int mode_missing;
static int wide_equal(const W *s,const char *a){while(*s&&*a){if(*s++!=(unsigned char)*a++)return 0;}return !*s&&!*a;}
H CreateFileW(const W *p,U32 access,U32 share,void *security,U32 disposition,U32 flags,H template){
    (void)security;(void)template;assert(share==3 && flags==0x80);
    if(wide_equal(p,"\\Storage Card\\P2DGLES\\mode.txt")){
        assert(access==0x80000000 && disposition==3);mode_opens++;
        return mode_missing?(H)-1:(H)1;
    }
    assert(access==0x40000000 && disposition==2);transport_writes++;
    if(wide_equal(p,"\\Storage Card\\P2DGLES\\guest.log"))return (H)2;
    assert(wide_equal(p,"\\Storage Card\\P2DGLES\\session.reset"));return (H)3;
}
int ReadFile(H h,void *p,U32 n,U32 *done,void *o){
    (void)o;assert(h==(H)1);mode_reads++;*done=0;
    if(fail_read && mode_reads==fail_read)return 0;
    if(n>mode_size-mode_offset)n=mode_size-mode_offset;
    if(mode_chunk && n>mode_chunk)n=mode_chunk;
    memcpy(p,mode_data+mode_offset,n);mode_offset+=n;*done=n;return 1;
}
int WriteFile(H h,const void *p,U32 n,U32 *done,void *o){(void)p;(void)o;assert(h==(H)2||h==(H)3);*done=n;return 1;}
int CloseHandle(H h){if(h==(H)1)mode_closes++;return 1;}
H LoadLibraryW(const W *p){if(wide_equal(p,"ceddk.dll"))return 0;assert(wide_equal(p,"coredll.dll"));return (H)5;}
static H fake_mutex(void *a,int owner,const W *name){(void)a;(void)name;assert(owner==1);session_claims++;return (H)4;}
static U32 fake_error(void){return 0;}
static U32 fake_pid(void){return 123;}
static int fake_free(void *p,U32 n,U32 type){assert(!n&&type==0x8000);(void)p;return 1;}
static I32 fake_metrics(int i){return i?320:240;}
static void *fake_alloc(void *address,U32 n,U32 type,U32 protect){
    (void)address;assert(type==0x3000&&protect==4);
    void *p=mmap(0,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    assert(p!=MAP_FAILED && (uintptr_t)p<=UINT32_MAX-n);return p;
}
void *GetProcAddressW(H h,const W *p){
    assert(h==(H)5);
    if(wide_equal(p,"CreateMutexW"))return fake_mutex;
    if(wide_equal(p,"GetLastError"))return fake_error;
    if(wide_equal(p,"VirtualAlloc"))return fake_alloc;
    if(wide_equal(p,"VirtualFree"))return fake_free;
    if(wide_equal(p,"GetCurrentProcessId"))return fake_pid;
    if(wide_equal(p,"GetSystemMetrics"))return fake_metrics;
    assert(wide_equal(p,"GetClientRect"));return 0;
}
void Sleep(U32 n){(void)n;abort();}
static U32 invoke(U32 op,U32 a,U32 b){U32 r[4]={a,b,0,0};return bridge(op,r,0,2);}
static U32 reported_units(void){
    U32 *out=fake_alloc(0,4096,0x3000,4);*out=0xdeadbeef;
    invoke(OP_glGetIntegerv,0x84e2,(U32)(uintptr_t)out);U32 result=*out;munmap(out,4096);return result;
}
static void selectors(U32 maximum){
    assert(sizeof(texture_enabled)/sizeof(texture_enabled[0])==4);
    assert(sizeof(texcoord)/sizeof(texcoord[0])==4);
    for(U32 unit=0;unit<maximum;unit++){
        U32 before=records;invoke(OP_glActiveTexture,0x84c0+unit,0);
        invoke(OP_glClientActiveTexture,0x84c0+unit,0);
        assert(glerror==0 && server_unit==unit && client_unit==unit && records==before+2);
    }
    for(U32 unit=maximum;unit<5;unit++){
        U32 before=records;invoke(OP_glActiveTexture,0x84c0+unit,0);
        assert(glerror==0x500 && server_unit==maximum-1 && records==before);glerror=0;
        invoke(OP_glClientActiveTexture,0x84c0+unit,0);
        assert(glerror==0x500 && client_unit==maximum-1 && records==before);glerror=0;
    }
    invoke(OP_glActiveTexture,0x84bf,0);assert(glerror==0x500 && server_unit==maximum-1);glerror=0;
    invoke(OP_glClientActiveTexture,0x84bf,0);assert(glerror==0x500 && client_unit==maximum-1);glerror=0;
}
int main(int argc,char **argv){
    assert(argc==5);mode_missing=!strcmp(argv[1],"-");
    if(!mode_missing){size_t n=strlen(argv[1]);assert(!(n&1)&&n/2<=sizeof(mode_data));
        for(size_t i=0;i<n;i+=2){unsigned x;assert(sscanf(argv[1]+i,"%2x",&x)==1);mode_data[mode_size++]=(unsigned char)x;}}
    mode_chunk=(U32)atoi(argv[2]);fail_read=(U32)atoi(argv[3]);U32 want=(U32)atoi(argv[4]);
    if(!want){assert(!init() && failed && initialized);assert(!transport_writes);assert(!session_claims);assert(mode_opens==1);puts("PASS inactive transport has no writes or renderer ownership");return 0;}
    assert(reported_units()==want);assert(!failed && initialized);assert(transport_writes==2);
    selectors(want);
    assert(!strcmp((char*)(uintptr_t)invoke(OP_glGetString,0x1f00,0),"Pocket2Droid"));
    assert(!strcmp((char*)(uintptr_t)invoke(OP_glGetString,0x1f01,0),"Pocket2Droid native GLES bridge"));
    assert(!strcmp((char*)(uintptr_t)invoke(OP_eglQueryString,1,0x3053),"Pocket2Droid native GLES bridge"));
    /* Mode changes during a DLL lifetime cannot change advertised limits. */
    memcpy(mode_data,want==2?"on cpu":"on gpu",6);mode_size=6;mode_offset=0;
    U32 opens=mode_opens,reads=mode_reads;
    assert(reported_units()==want);assert(mode_opens==opens&&mode_reads==reads);
    assert(opens==1&&mode_closes==(mode_missing?0:1));puts("PASS");return 0;
}
