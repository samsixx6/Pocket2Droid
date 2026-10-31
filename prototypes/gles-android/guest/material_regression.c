/* Appended to the real bridge and existing WinCE API test fixture. */
static U32 material_invoke(U32 op,U32 face,U32 pname,U32 value){U32 r[4]={face,pname,value,0};return bridge(op,r,0,3);}
static void material_clear(void){used=24;records=0;glerror=0;}
static void material_expect(U32 op,U32 pname,const U32 *values,U32 count){
 assert(!glerror&&records==1&&used==96);U32*r=(U32*)((unsigned char*)packet+24);
 assert(r[0]==op&&r[1]==0x408&&r[2]==pname&&r[17]==0);
 for(U32 i=0;i<count;i++)assert(r[3+i]==values[i]);
}
int main(void){
 /* ROM DLLs intentionally stay inactive without an explicit transport mode. */
 mode_missing=0;memcpy(mode_data,"on gpu",6);mode_size=6;assert(init());U32 *p=fake_alloc(0,8192,0x3000,4);assert(!mprotect((unsigned char*)p+4096,4096,PROT_NONE));
 U32 *one=(U32*)((unsigned char*)p+4092);*one=32u*65536u;
 /* One parameter at a protected-page boundary: shininess must never read4. */
 material_clear();material_invoke(98,0x408,0x1601,(U32)(uintptr_t)one);material_expect(98,0x1601,one,1);
 const U32 fixed[]={32768,65536,0xffff0000u,131072};memcpy(p,fixed,sizeof(fixed));
 const U32 pnames[]={0x1200,0x1201,0x1202,0x1600,0x1602};
 for(unsigned i=0;i<5;i++){material_clear();material_invoke(98,0x408,pnames[i],(U32)(uintptr_t)p);material_expect(98,pnames[i],fixed,4);}
 const U32 floats[]={0xbf000000u,0x3e800000u,0x3f800000u,0x40000000u};memcpy(p,floats,sizeof(floats));
 material_clear();material_invoke(96,0x408,0x1201,(U32)(uintptr_t)p);material_expect(96,0x1201,floats,4);
 const U32 scalar[]={0x42000000u};material_clear();material_invoke(95,0x408,0x1601,scalar[0]);material_expect(95,0x1601,scalar,1);
 const U32 scalarx[]={32u*65536u};material_clear();material_invoke(97,0x408,0x1601,scalarx[0]);material_expect(97,0x1601,scalarx,1);
 const U32 bad[][4]={{98,0x404,0x1202,1},{98,0x405,0x1202,1},{98,0x408,0x1603,1},{97,0x408,0x1200,1},{95,0x408,0x1602,1},{98,0x408,0x1601,0},{96,0x408,0x1201,0},{97,0x408,0x1601,0xffff0000u},{97,0x408,0x1601,129u*65536u},{95,0x408,0x1601,0xbf800000u},{95,0x408,0x1601,0x43010000u}};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){material_clear();material_invoke(bad[i][0],bad[i][1],bad[i][2],bad[i][3]);assert(glerror==(i<5?0x500u:0x501u)&&!records&&used==24);}
 material_clear();*one=129u*65536u;material_invoke(98,0x408,0x1601,(U32)(uintptr_t)one);assert(glerror==0x501&&!records);
 material_clear();*one=0x43010000u;material_invoke(96,0x408,0x1601,(U32)(uintptr_t)one);assert(glerror==0x501&&!records);
 munmap(p,8192);puts("PASS actual guest material bounds, values, forms, errors");return 0;
}
