/* Actual bridge, appended to the WinCE API fixture. Guard pages catch overreads. */
static void lighting_clear(void){used=24;records=0;glerror=0;}
static void lighting_call(U32 op,U32 target,U32 pname,U32 value){U32 r[4]={target,pname,value,0};bridge(op,r,0,op<85?2:3);}
static void lighting_expect(U32 op,U32 target,U32 pname,const U32*p,U32 n){
 assert(!glerror&&records==1);U32*r=(U32*)((unsigned char*)packet+24);
 assert(r[0]==op&&r[1]==target);if(op>=85)assert(r[2]==pname);
 for(U32 i=0;i<n;i++)assert(r[(op<85?2:3)+i]==p[i]);
}
int main(void){
 memcpy(mode_data,"on gpu",6);mode_size=6;assert(init());U32*p=fake_alloc(0,8192,0x3000,4);assert(!mprotect((unsigned char*)p+4096,4096,PROT_NONE));unsigned count=0;
 for(U32 op=81;op<=88;op++){
  int vector=!(op&1),model=op<85;U32 n=vector?4:1,*v=p+1024-n;
  for(U32 i=0;i<n;i++)v[i]=(op==83||op==84||op==87||op==88)?(i+1)*32768:0x3f000000u+i*0x800000u;
  U32 target=model?(vector?0xb53:0xb52):0x4001,pname=vector?0x1201:0x1206;
  lighting_clear();lighting_call(op,target,model?(vector?(U32)(uintptr_t)v:v[0]):pname,vector?(U32)(uintptr_t)v:v[0]);
  lighting_expect(op,target,pname,v,n);count++;
 }
 const U32 pnames[]={0x1200,0x1201,0x1202,0x1203,0x1204,0x1205,0x1206,0x1207,0x1208,0x1209};
 for(unsigned j=0;j<10;j++){U32 n=j<4?4:j==4?3:1,*v=p+1024-n;for(U32 i=0;i<n;i++)v[i]=(i+1)*65536;
  lighting_clear();lighting_call(88,0x4000,pnames[j],(U32)(uintptr_t)v);lighting_expect(88,0x4000,pnames[j],v,n);count++;
 }
 lighting_clear();lighting_call(88,0x4000,0x1203,0);assert(glerror==0x501&&!records);count++;
 lighting_clear();lighting_call(84,0xb53,0,0);assert(glerror==0x501&&!records);count++;
 munmap(p,8192);printf("PASS %u actual guest lighting forms/vector lengths/null checks\n",count);return 0;
}
