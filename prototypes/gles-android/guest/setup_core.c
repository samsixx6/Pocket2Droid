/* Portable production scanner and reversible file policy. No OS calls here. */
#include "setup_core.h"
#include "supported_apis.h"
static void zero(void *v,U32 n){U8 *p=v;while(n--)*p++=0;}
static void copy(void *v,const void *s,U32 n){U8 *d=v;const U8*p=s;while(n--)*d++=*p++;}
static int equal(const void *a,const void *b,U32 n){const U8*x=a,*y=b;U32 d=0;while(n--)d|=*x++^*y++;return d==0;}
static U32 u16(const U8*p){return p[0]|((U32)p[1]<<8);}
static U32 u32(const U8*p){return u16(p)|(u16(p+2)<<16);}
static U32 rr(U32 x,U32 n){return(x>>n)|(x<<(32-n));}
static void hashblock(P2DHash*h){
 static const U32 k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 U32 w[64],a,b,c,d,e,f,g,t,z;for(U32 i=0;i<16;i++)w[i]=((U32)h->block[i*4]<<24)|((U32)h->block[i*4+1]<<16)|((U32)h->block[i*4+2]<<8)|h->block[i*4+3];
 for(U32 i=16;i<64;i++)w[i]=w[i-16]+(rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10));
 a=h->state[0];b=h->state[1];c=h->state[2];d=h->state[3];e=h->state[4];f=h->state[5];g=h->state[6];z=h->state[7];
 for(U32 i=0;i<64;i++){t=z+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+k[i]+w[i];U32 q=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));z=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+q;}
 h->state[0]+=a;h->state[1]+=b;h->state[2]+=c;h->state[3]+=d;h->state[4]+=e;h->state[5]+=f;h->state[6]+=g;h->state[7]+=z;
}
void p2d_hash_start(P2DHash*h){static const U32 initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};zero(h,sizeof(*h));copy(h->state,initial,32);}
void p2d_hash_update(P2DHash*h,const void*v,U32 n){const U8*p=v;h->length+=n;while(n--){h->block[h->used++]=*p++;if(h->used==64){hashblock(h);h->used=0;}}}
void p2d_hash_end(P2DHash*h,U8 out[32]){U32 length=h->length;U8 tail[72];zero(tail,sizeof(tail));tail[0]=128;U32 pad=h->used<56?56-h->used:120-h->used;p2d_hash_update(h,tail,pad);U8 bits[8]={0,0,0,0,(U8)(length>>21),(U8)(length>>13),(U8)(length>>5),(U8)(length<<3)};p2d_hash_update(h,bits,8);for(U32 i=0;i<32;i++)out[i]=(U8)(h->state[i>>2]>>(24-8*(i&3)));}
void p2d_sha256(const void*v,U32 n,U8 o[32]){P2DHash h;p2d_hash_start(&h);p2d_hash_update(&h,v,n);p2d_hash_end(&h,o);}

typedef struct {const U8*p;U32 n,section,count;} PE;
static int range(U32 at,U32 count,U32 n){return at<=n&&count<=n-at;}
static U32 rva(PE*q,U32 at,U32 size){
 U32 result=0xffffffffu;
 for(U32 i=0;i<q->count;i++){const U8*s=q->p+q->section+i*40;U32 va=u32(s+12),raw=u32(s+20),len=u32(s+16);if(at>=va&&at-va<=len&&size<=len-(at-va)&&range(raw,at-va,q->n)&&range(raw+at-va,size,q->n)){if(result!=0xffffffffu)return 0xffffffffu;result=raw+at-va;}}
 return result;
}
static int astr(PE*q,U32 at,char out[128]){U32 pos=rva(q,at,1);if(pos==0xffffffffu)return 0;for(U32 i=0;i<128;i++){if(at>0xffffffffu-i)return 0;U32 k=rva(q,at+i,1);if(k==0xffffffffu||k!=pos+i)return 0;out[i]=(char)q->p[k];if(!out[i])return 1;}return 0;}
static int strsame(const char*a,const char*b,int lower){for(U32 i=0;i<128;i++){U32 x=(U8)a[i],y=(U8)b[i];if(lower){if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;}if(x!=y)return 0;if(!x)return 1;}return 0;}
int p2d_pe_profile(const U8*p,U32 n){
 if(n<64||p[0]!='M'||p[1]!='Z')return -1;U32 head=u32(p+60);if(!range(head,24,n)||u32(p+head)!=0x4550)return -1;
 U32 machine=u16(p+head+4),opt=head+24,os=u16(p+head+20),count=u16(p+head+6);if((machine!=0x1c0&&machine!=0x1c2)||(u16(p+head+22)&0x2000)||!count||count>96||os<224||!range(opt,os,n)||u16(p+opt)!=0x10b||u16(p+opt+68)!=9||u32(p+opt+92)<2)return -1;
 U32 sections=opt+os;if(!range(sections,count*40,n))return -1;PE q={p,n,sections,count};
 for(U32 i=0;i<count;i++){const U8*s=p+sections+i*40;if(!range(u32(s+20),u32(s+16),n))return -1;}
 U32 imports=u32(p+opt+104),size=u32(p+opt+108);if(!imports)return 0;if(size<20||size>81920)return -1;int mask=0;
 for(U32 i=0;i<size/20&&i<4096;i++){
  if(imports>0xffffffffu-i*20)return -1;U32 off=rva(&q,imports+i*20,20);if(off==0xffffffffu)return -1;const U8*d=p+off;
  if(!u32(d)&&!u32(d+4)&&!u32(d+8)&&!u32(d+12)&&!u32(d+16))return mask;
  char dll[128];if(!astr(&q,u32(d+12),dll))return -1;int profile=strsame(dll,"libGLES_CL.dll",1)?1:strsame(dll,"libGLES_CM.dll",1)?2:0;
  if(!profile){if(strsame(dll,"libEGL.dll",1)||strsame(dll,"GLES.dll",1))return -2;continue;}
  U32 thunk=u32(d)?u32(d):u32(d+16);if(!thunk)return -1;int finished=0;
  for(U32 j=0;j<2048;j++){if(thunk>0xffffffffu-j*4)return -1;U32 t=rva(&q,thunk+j*4,4);if(t==0xffffffffu)return -1;U32 v=u32(p+t);if(!v){finished=1;break;}int found=0;
   if(v&0x80000000u){if((v&0x7fff0000u)||!(v&65535))return -1;for(U32 k=0;k<P2D_API_COUNT;k++)if((profile==1?p2d_apis[k].cl:p2d_apis[k].cm)==(v&65535)){found=1;break;}}
   else{char api[128];if(v>0xfffffffdu||!astr(&q,v+2,api))return -1;for(U32 k=0;k<P2D_API_COUNT;k++)if(strsame(api,p2d_apis[k].name,0)&&(profile==1?p2d_apis[k].cl:p2d_apis[k].cm)){found=1;break;}}
   if(!found)return -2;
  }if(!finished)return -1;mask|=profile;
 }return -1;
}

typedef struct {U8 magic[8];U32 version,original;U8 prior[32],installed[32],checksum[32];} Record;
static const U8 magic[8]={'P','2','D','G','L','E','S','1'};
static int suffix(W out[512],const W *p,const char*s){U32 n=0;while(*p){if(n>=450)return 0;out[n++]=*p++;}while(*s){if(n>=511)return 0;out[n++]=(U8)*s++;}out[n]=0;return 1;}
static int readrecord(const P2DIO*io,const W*owner,Record*r){U8 hash[32];int status=io->hash(io->ctx,owner,hash);if(status)return status==1?1:-1;if(io->read(io->ctx,owner,r,sizeof(*r)))return -1;U8 sum[32];p2d_sha256(r,sizeof(*r)-32,sum);return equal(r->magic,magic,8)&&r->version==1&&r->original<=1&&equal(sum,r->checksum,32)?0:-1;}
static int samehash(const P2DIO*io,const W*p,const U8 want[32]){U8 h[32];return io->hash(io->ctx,p,h)==0&&equal(h,want,32);}
static int install_common(const P2DIO*io,const W*target,const W*asset,const void*data,U32 size,const U8 installed[32]){
 W owner[512],backup[512];U8 current[32],bh[32];Record rec;if(!suffix(owner,target,".p2d-gles-v1.owner")||!suffix(backup,target,".p2d-gles-v1.backup"))return -1;
 int have=readrecord(io,owner,&rec);if(have<0)return -2;int present=io->hash(io->ctx,target,current);if(present<0)return -1;
 if(have==1){
  if(!present&&equal(current,installed,32))return 3;
  if(io->hash(io->ctx,backup,bh)!=1)return -2;
  zero(&rec,sizeof(rec));copy(rec.magic,magic,8);rec.version=1;rec.original=!present;if(!present)copy(rec.prior,current,32);copy(rec.installed,installed,32);p2d_sha256(&rec,sizeof(rec)-32,rec.checksum);
  if(io->putnew(io->ctx,owner,&rec,sizeof(rec))||readrecord(io,owner,&rec))return -1;
 }else if(!equal(rec.installed,installed,32))return -2;
 if(!present&&equal(current,rec.installed,32))return 2;
 if(!present){
  if(!rec.original||!equal(current,rec.prior,32)||io->hash(io->ctx,backup,bh)!=1)return -2;
  if(!samehash(io,target,rec.prior)||io->movenew(io->ctx,target,backup))return -1;
  if(!samehash(io,backup,rec.prior)){io->movenew(io->ctx,backup,target);return -2;}
 }else if(rec.original&&!samehash(io,backup,rec.prior))return -2;
 if((asset?io->copynew(io->ctx,asset,target):io->putnew(io->ctx,target,data,size))||!samehash(io,target,rec.installed)||io->writable(io->ctx,target))return -1;return 1;
}
int p2d_install(const P2DIO*io,const W*target,const W*asset){U8 hash[32];if(io->hash(io->ctx,asset,hash))return -1;return install_common(io,target,asset,0,0,hash);}
int p2d_install_blob(const P2DIO*io,const W*target,const void*data,U32 n){U8 hash[32];p2d_sha256(data,n,hash);return install_common(io,target,0,data,n,hash);}
int p2d_restore(const P2DIO*io,const W*target){
 W owner[512],backup[512];Record rec;U8 current[32],b[32];if(!suffix(owner,target,".p2d-gles-v1.owner")||!suffix(backup,target,".p2d-gles-v1.backup"))return -1;
 int have=readrecord(io,owner,&rec);if(have==1)return 0;if(have<0)return -2;int present=io->hash(io->ctx,target,current);if(present<0)return -1;
 if(!present&&!equal(current,rec.installed,32)){
  if(rec.original&&equal(current,rec.prior,32)&&io->hash(io->ctx,backup,b)==1){return io->remove(io->ctx,owner)?-1:1;}
  return -2;
 }
 if(rec.original&&!samehash(io,backup,rec.prior))return -2;
 if(!present){if(!samehash(io,target,rec.installed)||io->writable(io->ctx,target)||io->remove(io->ctx,target))return -1;}
 if(rec.original&&(io->movenew(io->ctx,backup,target)||!samehash(io,target,rec.prior)))return -1;
 if(io->remove(io->ctx,owner))return -1;return 1;
}
