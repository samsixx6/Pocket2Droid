#include "command-fixture.c"
static void call(U32 op,U32*a,U32 n){bridge(op,a,a+4,n);}
static void c4(U32 op,U32 a,U32 b,U32 c,U32 d){U32 v[4]={a,b,c,d};call(op,v,4);}
typedef struct{float unused,x,y,z;unsigned char rgba[4];float s,t;} V;
int main(int argc,char**argv){
 assert(argc==2);int vbo=!strcmp(argv[1],"vbo");
 V*data=fake_alloc(0,4096,0x3000,4);V values[]={{9,-1,-1,0,{255,0,0,255},0,0},{9,-.1,-1,0,{255,0,0,255},0,0},{9,-.55,1,0,{255,0,0,255},0,0}};memcpy(data,values,sizeof(values));
 unsigned char*extra=(unsigned char*)data+512;memset(extra,255,16);unsigned short idx[]={99,99,0,1,2};memcpy(extra+32,idx,sizeof(idx));
 c4(OP_glClearColor,0,0,0,0);c4(OP_glClear,0x4000,0,0,0);c4(OP_glViewport,0,0,64,64);width=height=64;
 c4(OP_glActiveTexture,0x84c1,0,0,0);c4(OP_glBindTexture,0xde1,1,0,0);c4(OP_glEnable,0xde1,0,0,0);
 U32 tex[]={0xde1,0,0x1908,2,2,0,0x1908,0x1401,(U32)extra};call(OP_glTexImage2D,tex,9);
 if(vbo){c4(OP_glBindBuffer,0x8892,7,0,0);c4(OP_glBufferData,0x8892,sizeof(values),(U32)data,0x88e4);}
 U32 base=vbo?0:(U32)data;c4(OP_glVertexPointer,3,0x1406,sizeof(V),base+4);c4(OP_glColorPointer,4,0x1401,sizeof(V),base+16);c4(OP_glEnableClientState,0x8074,0,0,0);c4(OP_glEnableClientState,0x8076,0,0,0);
 c4(OP_glClientActiveTexture,0x84c1,0,0,0);c4(OP_glTexCoordPointer,2,0x1406,sizeof(V),base+20);c4(OP_glEnableClientState,0x8078,0,0,0);c4(OP_glClientActiveTexture,0x84c0,0,0,0);
 if(vbo){c4(OP_glBindBuffer,0x8892,0,0,0);c4(OP_glBindBuffer,0x8893,8,0,0);c4(OP_glBufferData,0x8893,sizeof(idx),(U32)(extra+32),0x88e4);}
 c4(OP_glDrawElements,4,3,0x1403,vbo?4:(U32)(extra+36));
 for(unsigned i=0;i<3;i++){data[i].x+=1.05f;data[i].rgba[0]=0;data[i].rgba[2]=255;}
 if(vbo){c4(OP_glBindBuffer,0x8892,7,0,0);c4(OP_glBufferSubData,0x8892,0,sizeof(values),(U32)data);c4(OP_glBufferData,0x8892,sizeof(values),(U32)data,0x88e8);c4(OP_glBindBuffer,0x8892,0,0,0);}
 c4(OP_glDrawElements,4,3,0x1403,vbo?4:(U32)(extra+36));assert(!glerror&&client_unit==0);
 packet[0]=0x314c4750;packet[1]=7;packet[2]=records;packet[3]=width;packet[4]=height;packet[5]=used;assert(fwrite(packet,1,used,stdout)==used);return 0;
}
