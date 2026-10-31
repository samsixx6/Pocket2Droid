/* Throwaway PC experiment: full WM6 GLES DLL -> native PocketHLE renderer.
 * Single process/context, bounded file RPC. Not a production driver.
 */
typedef unsigned long U32;
typedef long I32;
typedef unsigned short W;
typedef void *H;
extern H CreateFileW(const W*,U32,U32,void*,U32,U32,H);
extern int WriteFile(H,const void*,U32,U32*,void*);
extern int ReadFile(H,void*,U32,U32*,void*);
extern int CloseHandle(H);
extern H LoadLibraryW(const W*);
extern void *GetProcAddressW(H,const W*);
extern void Sleep(U32);
#include "bridge_ops.h"
#define MAX_PACKET 4194304u
#define RPC L"\\Storage Card\\P2DGLES\\"
static H module,core,logfile,window;
static U32 *packet,used=24,records,sequence,glerror,eglerror=0x3000,next_texture=1;
static U32 width=240,height=320,initialized,failed,pbuffer,unpack_alignment=4;
static unsigned char *pixels;
static U32 pixels_bytes,pixels_capacity,client_unit;
typedef struct {U32 pointer,size,type,stride,enabled;} Array;
static Array vertex,color,normal,texcoord[4];
static void *(*alloc)(void*,U32,U32,U32);
static int (*getrect)(H,I32*);
static I32 (*metrics)(int);
static void bytes(void *d,const void *s,U32 n){unsigned char *a=d;const unsigned char *b=s;while(n--)*a++=*b++;}
static void zero(void *d,U32 n){volatile unsigned char *p=d;while(n--)*p++=0;}
void __aeabi_memclr4(void *d,U32 n){zero(d,n);}
void __aeabi_memcpy4(void *d,const void *s,U32 n){bytes(d,s,n);}
void __aeabi_memcpy(void *d,const void *s,U32 n){bytes(d,s,n);}
U32 __aeabi_uidiv(U32 n,U32 d){U32 q=0,r=0;if(!d)return 0;for(int i=31;i>=0;i--){U32 high=r>>31;r=(r<<1)|((n>>i)&1);if(high||r>=d){r-=d;q|=1u<<i;}}return q;}
static U32 length(const char*s){U32 n=0;while(s[n])n++;return n;}
static void text(const char*s){U32 n;if(logfile&&logfile!=(H)-1)WriteFile(logfile,s,length(s),&n,0);}
static void hex(U32 v){char b[11];U32 i;b[0]='0';b[1]='x';b[10]='\n';for(i=0;i<8;i++)b[i+2]="0123456789abcdef"[(v>>(28-4*i))&15];U32 n;WriteFile(logfile,b,11,&n,0);}
static void *sym(const W *s){return GetProcAddressW(core,s);}
static void error(U32 e){if(!glerror)glerror=e;}
static int init(void){
 if(initialized)return !failed;initialized=1;
 logfile=CreateFileW(RPC L"guest.log",0x40000000,3,0,2,0x80,0);
 text("Full WM6 PocketHLE bridge started\n");
 core=LoadLibraryW(L"coredll.dll");alloc=(void*(*)(void*,U32,U32,U32))sym(L"VirtualAlloc");
 getrect=(int(*)(H,I32*))sym(L"GetClientRect");metrics=(I32(*)(int))sym(L"GetSystemMetrics");
 if(!alloc){failed=1;return 0;}
 packet=alloc(0,MAX_PACKET,0x3000,4);pixels_capacity=1024*1024*2;pixels=alloc(0,pixels_capacity,0x3000,4);
 if(!packet||!pixels){failed=1;text("allocation failed\n");return 0;}
 vertex.size=4;vertex.type=0x1406;color.size=4;color.type=0x1406;normal.size=3;normal.type=0x1406;
 for(U32 i=0;i<4;i++){texcoord[i].size=4;texcoord[i].type=0x1406;}
 if(metrics){width=metrics(0);height=metrics(1);}
 text("initial dimensions\n");hex(width);hex(height);return 1;
}
static int transfer(H f,void *p,U32 n,int writing){
 unsigned char *b=p;while(n){U32 done=0,part=n>32768?32768:n;int ok=writing?WriteFile(f,b,part,&done,0):ReadFile(f,b,part,&done,0);if(!ok||!done||done>part){text(writing?"transfer write failed\n":"transfer read failed\n");hex(n);hex(part);hex(done);hex(ok);return 0;}b+=done;n-=done;}return 1;
}
static int flush(void){
 H f;U32 header[6],seq=++sequence,n;
 if(failed)return 0;
 packet[0]=0x314c4750;packet[1]=seq;packet[2]=records;packet[3]=width;packet[4]=height;packet[5]=used;
 f=CreateFileW(RPC L"request.bin",0x40000000,3,0,2,0x80,0);
 if(f==(H)-1||!transfer(f,packet,used,1)){if(f!=(H)-1)CloseHandle(f);failed=1;text("request write failed\n");return 0;}CloseHandle(f);
 f=CreateFileW(RPC L"request.ready",0x40000000,3,0,2,0x80,0);
 if(f==(H)-1){failed=1;return 0;}int ok=WriteFile(f,&seq,4,&n,0)&&n==4;CloseHandle(f);if(!ok){failed=1;return 0;}
 used=24;records=0;
 for(U32 tries=0;tries<6000;tries++){
  f=CreateFileW(RPC L"response.bin",0x80000000,3,0,3,0x80,0);
  if(f!=(H)-1){
   ok=ReadFile(f,header,24,&n,0)&&n==24;
   if(ok&&header[0]==0x31524750&&header[1]==seq){
    if(header[2]!=width||header[3]!=height||header[4]!=width*height*2||header[4]>pixels_capacity){CloseHandle(f);failed=1;text("bad response dimensions\n");return 0;}
    pixels_bytes=header[4];ok=transfer(f,pixels,pixels_bytes,0);CloseHandle(f);
    if(!ok){failed=1;text("short response\n");return 0;}
    if(header[5]){text("host GL error\n");hex(header[5]);error(header[5]);}
    if(seq<=4){text("completed batch\n");hex(seq);}return 1;
   }CloseHandle(f);
  }Sleep(10);
 }
 failed=1;text("host timeout\n");error(0x502);return 0;
}
static int record(U32 op,const U32 *args,const void *data,U32 size){
 U32 padded=(size+3)&~3u;if(size>MAX_PACKET-96){error(0x505);return 0;}
 if(used+72+padded>MAX_PACKET&&!flush())return 0;
 U32 *r=(U32*)((unsigned char*)packet+used);r[0]=op;for(U32 i=0;i<16;i++)r[1+i]=args?args[i]:0;r[17]=size;
 if(size){if(data)bytes(r+18,data,size);else zero(r+18,size);}if(padded>size)zero((unsigned char*)(r+18)+size,padded-size);
 used+=72+padded;records++;return 1;
}
static U32 component(U32 type){switch(type){case 0x1400:case 0x1401:return 1;case 0x1402:case 0x1403:return 2;case 0x1406:case 0x140c:return 4;default:return 0;}}
static int snapshot(U32 pointer,U32 size){U32 a[16];zero(a,sizeof(a));if(!pointer||!size)return 1;a[0]=pointer;a[1]=size;return record(0x10000,a,(void*)pointer,size);}
static int array_snapshot(Array *p,U32 count){
 if(!p->enabled)return 1;U32 c=component(p->type);if(!c||!p->pointer||!p->size||p->size>4||count>1048576){error(0x501);return 0;}
 U32 stride=p->stride?p->stride:p->size*c;if(stride>MAX_PACKET||!count)return !count;
 if(count-1>(MAX_PACKET-p->size*c)/stride){error(0x505);return 0;}
 return snapshot(p->pointer,(count-1)*stride+p->size*c);
}
static int draw_snapshot(U32 count){
 if(!array_snapshot(&vertex,count)||!array_snapshot(&color,count)||!array_snapshot(&normal,count))return 0;
 for(U32 i=0;i<4;i++)if(!array_snapshot(&texcoord[i],count))return 0;return 1;
}
static void present(void){
 if(pbuffer||!window||!pixels_bytes)return;
 typedef struct {U32 size;I32 w,h;unsigned short planes,bpp;U32 compression,sizeimage;I32 x,y;U32 used,important,masks[3];} Bitmap;
 H (*getdc)(H)=(H(*)(H))sym(L"GetDC");int (*releasedc)(H,H)=(int(*)(H,H))sym(L"ReleaseDC");
 H (*createdc)(H)=(H(*)(H))sym(L"CreateCompatibleDC");H (*createdib)(H,Bitmap*,U32,void**,H,U32)=(H(*)(H,Bitmap*,U32,void**,H,U32))sym(L"CreateDIBSection");
 H (*select)(H,H)=(H(*)(H,H))sym(L"SelectObject");int (*blit)(H,int,int,int,int,H,int,int,U32)=(int(*)(H,int,int,int,int,H,int,int,U32))sym(L"BitBlt");
 int (*delobj)(H)=(int(*)(H))sym(L"DeleteObject");int (*deldc)(H)=(int(*)(H))sym(L"DeleteDC");
 if(!getdc||!createdc||!createdib||!select||!blit||!delobj||!deldc||!releasedc){failed=1;text("missing GDI export\n");return;}
 Bitmap b;zero(&b,sizeof(b));b.size=40;b.w=width;b.h=-(I32)height;b.planes=1;b.bpp=16;b.compression=3;b.sizeimage=pixels_bytes;b.masks[0]=0xf800;b.masks[1]=0x7e0;b.masks[2]=31;
 H dc=getdc(window),mem=dc?createdc(dc):0,bitmap;void *bits=0;
 if(!dc||!mem){failed=1;text("GDI DC failed\n");if(mem)deldc(mem);if(dc)releasedc(window,dc);return;}
 U32 rowbytes=width*2,stride=(rowbytes+3)&~3u;b.sizeimage=stride*height;
 bitmap=createdib(dc,&b,0,&bits,0,0);
 if(bitmap&&bits){H prior=select(mem,bitmap);for(U32 y=0;y<height;y++)bytes((unsigned char*)bits+y*stride,pixels+y*rowbytes,rowbytes);if(!prior||!blit(dc,0,0,width,height,mem,0,0,0xcc0020)){failed=1;text("GDI presentation failed\n");}select(mem,prior);delobj(bitmap);}else{failed=1;text("GDI bitmap failed\n");}
 deldc(mem);releasedc(window,dc);
}
static void dimensions(void){
 I32 rect[4];if(window&&getrect&&getrect(window,rect)&&rect[2]>0&&rect[3]>0){width=rect[2];height=rect[3];}
 if(!width||!height||width>2048||height>2048){failed=1;text("unsupported probe dimensions\n");}
}
static void pointer(Array *p,U32 *a){p->size=a[0];p->type=a[1];p->stride=a[2];p->pointer=a[3];}
static void enable_array(U32 which,U32 on){switch(which){case 0x8074:vertex.enabled=on;break;case 0x8076:color.enabled=on;break;case 0x8075:normal.enabled=on;break;case 0x8078:texcoord[client_unit].enabled=on;break;}}
static U32 config_attr(U32 attr){switch(attr){case 0x3020:return 16;case 0x3021:return 0;case 0x3022:return 5;case 0x3023:return 6;case 0x3024:return 5;case 0x3025:return 16;case 0x3028:return 1;case 0x3033:return 5;case 0x3040:return 1;case 0x3027:case 0x3034:return 0x3038;default:return 0;}}
U32 bridge(U32 op,const U32 *registers,const U32 *stack,U32 arity){
 U32 a[16];zero(a,sizeof(a));for(U32 i=0;i<arity&&i<16;i++)a[i]=i<4?registers[i]:stack[i-4];
 if(!init())return 0;
 switch(op){
 case OP_eglGetDisplay:return 1;
 case OP_eglInitialize:if(a[1])*(U32*)a[1]=1;if(a[2])*(U32*)a[2]=0;return 1;
 case OP_eglChooseConfig:if(a[2]&&a[3])*(U32*)a[2]=1;if(a[4])*(U32*)a[4]=a[2]&&!a[3]?0:1;return 1;
 case OP_eglGetConfigs:if(a[1]&&a[2])*(U32*)a[1]=1;if(a[3])*(U32*)a[3]=a[1]&&!a[2]?0:1;return 1;
 case OP_eglGetConfigAttrib:if(a[3])*(U32*)a[3]=config_attr(a[2]);return 1;
 case OP_eglCreateWindowSurface:window=(H)a[2];pbuffer=0;dimensions();text("window dimensions\n");hex(width);hex(height);return 2;
 case OP_eglCreatePbufferSurface:{U32 *p=(U32*)a[2];pbuffer=1;for(U32 i=0;p&&i<64&&p[i]!=0x3038;i+=2){if(p[i]==0x3057)width=p[i+1];if(p[i]==0x3056)height=p[i+1];}dimensions();return 2;}
 case OP_eglCreateContext:return 3;
 case OP_eglMakeCurrent:return 1;
 case OP_eglDestroyContext:case OP_eglDestroySurface:case OP_eglTerminate:case OP_eglWaitGL:case OP_eglWaitNative:case OP_eglSwapInterval:return 1;
 case OP_eglGetError:{U32 e=eglerror;eglerror=0x3000;return e;}
 case OP_eglGetCurrentContext:return 3;case OP_eglGetCurrentDisplay:return 1;case OP_eglGetCurrentSurface:return 2;
 case OP_eglQuerySurface:if(a[3])*(U32*)a[3]=a[2]==0x3057?width:a[2]==0x3056?height:a[2]==0x3028?1:0;return 1;
 case OP_eglQueryString:return (U32)(a[1]==0x3053?"PocketHLE PC bridge":a[1]==0x3054?"1.0":"");
 case OP_eglGetProcAddress:{const char *s=(char*)a[0];W name[128];U32 i=0;while(s&&s[i]&&i<127){name[i]=(unsigned char)s[i];i++;}name[i]=0;return (U32)GetProcAddressW(module,name);}
 case OP_eglSwapBuffers:if(!flush())return 0;present();return !failed;
 case OP_glGenTextures:{U32 *out=(U32*)a[1];if(a[0]>65536||!out){error(0x501);return 0;}for(U32 i=0;i<a[0];i++)out[i]=next_texture++;return 0;}
 case OP_glGetString:return (U32)(a[0]==0x1f00?"PocketHLE":a[0]==0x1f01?"PocketHLE native software PC bridge":a[0]==0x1f02?"OpenGL ES-CM 1.1":"");
 case OP_glGetError:{if(records)flush();U32 e=glerror;glerror=0;return e;}
 case OP_glGetIntegerv:if(a[1]){U32 *p=(U32*)a[1];*p=a[0]==0xd33?2048:a[0]==0x84e2?4:a[0]==0xd05?4:0;}return 0;
 case OP_glReadPixels:{if(!flush())return 0;U32 x=a[0],y=a[1],w=a[2],h=a[3];if(x>width||y>height||w>width-x||h>height-y||a[4]!=0x1908||a[5]!=0x1401||!a[6]){error(0x501);return 0;}
  unsigned char *d=(void*)a[6];for(U32 row=0;row<h;row++)for(U32 col=0;col<w;col++){U32 c=((unsigned short*)pixels)[(height-1-y-row)*width+x+col];*d++=((c>>11)&31)*255/31;*d++=((c>>5)&63)*255/63;*d++=(c&31)*255/31;*d++=255;}return 0;}
 case OP_glFlush:case OP_glFinish:return 0;
 case OP_glVertexPointer:pointer(&vertex,a);break;case OP_glColorPointer:pointer(&color,a);break;
 case OP_glTexCoordPointer:pointer(&texcoord[client_unit],a);break;
 case OP_glNormalPointer:normal.type=a[0];normal.stride=a[1];normal.pointer=a[2];break;
 case OP_glClientActiveTexture:if(a[0]<0x84c0||a[0]>=0x84c4){error(0x500);return 0;}client_unit=a[0]-0x84c0;break;
 case OP_glEnableClientState:enable_array(a[0],1);break;case OP_glDisableClientState:enable_array(a[0],0);break;
 case OP_glLoadMatrixx:case OP_glMultMatrixx:case OP_glLoadMatrixf:case OP_glMultMatrixf:if(!a[0]){error(0x501);return 0;}bytes(a,(void*)a[0],64);break;
 case OP_glFogxv:case OP_glFogfv:{U32 *p=(U32*)a[1];if(!p){error(0x501);return 0;}U32 n=a[0]==0xb66?4:1;for(U32 i=0;i<n;i++)a[1+i]=p[i];break;}
 case OP_glDeleteTextures:if(a[0]>65536){error(0x501);return 0;}record(op,a,(void*)a[1],a[0]*4);return 0;
 case OP_glTexImage2D:{U32 channels=a[6]==0x1908?4:a[6]==0x1907?3:a[6]==0x190a?2:1;U32 packed=a[7]==0x1401?channels:2;
  if(a[3]>4096||a[4]>4096||a[3]*a[4]>MAX_PACKET/packed){error(0x501);return 0;}
  U32 row=a[3]*packed,size=row*a[4],alignment=unpack_alignment;
  if(alignment!=1&&alignment!=2&&alignment!=4&&alignment!=8){error(0x501);return 0;}
  U32 stride=(row+alignment-1)&~(alignment-1);
  if(a[8]&&stride!=row){if(!record(op,a,0,size))return 0;unsigned char *dest=(unsigned char*)packet+used-((size+3)&~3u);for(U32 y=0;y<a[4];y++)bytes(dest+y*row,(unsigned char*)a[8]+y*stride,row);}
  else record(op,a,(void*)a[8],a[8]?size:0);return 0;}
 case OP_glDrawElements:{if(a[1]>1048576||!a[3]){error(0x501);return 0;}U32 bytesper=a[2]==0x1401?1:a[2]==0x1403?2:0;if(!bytesper){error(0x500);return 0;}
  U32 max=0;for(U32 i=0;i<a[1];i++){U32 n=bytesper==1?((unsigned char*)a[3])[i]:((unsigned short*)a[3])[i];if(n>max)max=n;}
  if(!snapshot(a[3],a[1]*bytesper)||!draw_snapshot(a[1]?max+1:0))return 0;break;}
 case OP_glDrawArrays:if(a[1]>1048576||a[2]>1048576-a[1]||!draw_snapshot(a[1]+a[2]))return 0;break;
 case OP_glPixelStorei:if(a[0]==0xcf5)unpack_alignment=a[1];return 0;
 default:if(!op_supported[op]){error(0x502);text("unsupported GL opcode\n");hex(op);return 0;}
 }
 record(op,a,0,0);return 0;
}
int _start(H h,U32 reason,void *reserved){(void)reserved;if(reason==1)module=h;return 1;}
