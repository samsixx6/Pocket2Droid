/* ARMv4 CE adapter; reusable scanner/ownership logic also runs in native tests. */
#include "ce_api.h"
#include "setup_core.c"
#define ROOT L"\\Storage Card\\P2DGLES\\"
#define SHORTCUT L"\\Windows\\Start Menu\\Programs\\Games\\Call of Duty 2 (Pocket2Droid GLES).lnk"
/* Microsoft WIN32_FIND_DATA, Windows CE: dwOID at36, cFileName at40. */
typedef struct {U32 attributes,times[6],sizehigh,sizelow,oid;W name[260];} FindData;
static H core;
static H (*findfirst)(const W*,FindData*);
static int (*findnext)(H,FindData*);
static int (*findclose)(H);
static int (*copyfile)(const W*,const W*,int);
static int (*movefile)(const W*,const W*);
static int (*deletefile)(const W*);
static U32 (*attributes)(const W*);
static int (*setattributes)(const W*,U32);
static U32 (*geterror)(void);
static U32 (*filesize)(H,U32*);
static int (*flushfile)(H);
static void*(*allocate)(void*,U32,U32,U32);
static int (*release)(void*,U32,U32);
static H (*createmutex)(void*,int,const W*);
static U32 entries,dirs,exes,totalbytes,found,applied,conflicts,failures;
static P2DIO io;
static int wjoin(W out[512],const W*a,const W*b){U32 n=0;while(*a){if(n>=449)return 0;out[n++]=*a++;}if(n&&out[n-1]!='\\')out[n++]='\\';while(*b){if(n>=450)return 0;out[n++]=*b++;}out[n]=0;return 1;}
static int wequal(const W*a,const W*b){for(U32 i=0;i<512;i++){U32 x=a[i],y=b[i];if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0;if(!x)return 1;}return 0;}
static int extension(const W*p){U32 n=0;while(p[n]&&n<260)n++;return n>=4&&wequal(p+n-4,L".exe");}
static int hfile(void*c,const W*p,U8 out[32]){(void)c;H f=CreateFileW(p,0x80000000,1,0,3,0x80,0);if(f==(H)-1){U32 error=geterror();return(error==2||error==3)?1:-1;}U8 block[4096];P2DHash h;p2d_hash_start(&h);U32 total=0,got;int ok=1;for(;;){got=0;if(!ReadFile(f,block,sizeof(block),&got,0)||got>sizeof(block)){ok=0;break;}if(!got)break;if(total>67108864-got){ok=0;break;}total+=got;p2d_hash_update(&h,block,got);}CloseHandle(f);if(!ok)return -1;p2d_hash_end(&h,out);return 0;}
static int rfile(void*c,const W*p,void*b,U32 n){(void)c;H f=CreateFileW(p,0x80000000,1,0,3,0x80,0);if(f==(H)-1)return -1;U32 got=0,tail=0;U8 extra;int ok=ReadFile(f,b,n,&got,0)&&got==n&&ReadFile(f,&extra,1,&tail,0)&&!tail;CloseHandle(f);return ok?0:-1;}
static int putfile(void*c,const W*p,const void*b,U32 n){(void)c;H f=CreateFileW(p,0x40000000,0,0,1,0x80,0);if(f==(H)-1)return -1;U32 done=0;int ok=WriteFile(f,b,n,&done,0)&&done==n&&flushfile(f);CloseHandle(f);return ok?0:-1;}
static int cfile(void*c,const W*s,const W*d){(void)c;return copyfile(s,d,1)?0:-1;}
static int mfile(void*c,const W*s,const W*d){(void)c;/* CE MoveFile fails if destination exists. */return movefile(s,d)?0:-1;}
static int dfile(void*c,const W*p){(void)c;return deletefile(p)?0:-1;}
static int writable(void*c,const W*p){(void)c;U32 a=attributes(p);if(a==0xffffffffu)return -1;return !(a&1)||setattributes(p,a&~1u)?0:-1;}
static int mode(void){
 H f=CreateFileW(ROOT L"mode.txt",0x80000000,3,0,3,0x80,0);if(f==(H)-1)return -1;
 char b[17];U32 n=0;int ok=1;
 while(n<sizeof(b)){U32 got=0,remaining=sizeof(b)-n;if(!ReadFile(f,b+n,remaining,&got,0)||got>remaining){ok=0;break;}if(!got)break;n+=got;}
 CloseHandle(f);if(!ok||n>16)return -1;
 while(n&&(b[n-1]=='\n'||b[n-1]=='\r'||b[n-1]==' '))n--;
 if(n>=2&&b[0]=='o'&&b[1]=='n'){
  if(n==2)return 1;
  if(n==6&&b[2]==' '&&b[4]=='p'&&b[5]=='u'&&(b[3]=='c'||b[3]=='g'))return 1;
 }
 if(n==3&&b[0]=='o'&&b[1]=='f'&&b[2]=='f')return 0;return -1;
}
static void result(int r){if(r==1)applied++;else if(r==-2)conflicts++;else if(r<0)failures++;}
static void shortcut(const W *executable){
 /* A collision with an unrelated personal shortcut is never adopted/replaced. */
 U8 h[32];if(io.hash(0,SHORTCUT,h)==0)return;
 char text[512];U32 count=0;while(executable[count]&&count<450){if(executable[count]>127)return;count++;}if(executable[count])return;
 U32 length=count+2,pos=0;if(length>=100){text[pos++]='0'+length/100;length=length%100;}if(count+2>=10){text[pos++]='0'+length/10;length=length%10;}text[pos++]='0'+length;text[pos++]='#';text[pos++]='"';for(U32 i=0;i<count;i++)text[pos++]=(char)executable[i];text[pos++]='"';
 result(p2d_install_blob(&io,SHORTCUT,text,pos));
}
static void scan(const W *directory,U32 depth,int on){
 if(depth>10||dirs++>=512||entries>=8192)return;
 W target[512],pattern[512];
 if(!on){if(wjoin(target,directory,L"libGLES_CL.dll"))result(p2d_restore(&io,target));if(wjoin(target,directory,L"libGLES_CM.dll"))result(p2d_restore(&io,target));}
 if(!wjoin(pattern,directory,L"*"))return;FindData data;H search=findfirst(pattern,&data);if(search==(H)-1)return;
 do{
  if(entries++>=8192)break;
  if(data.name[0]=='.'||wequal(data.name,L"P2DGLES")||wequal(data.name,L"2577"))continue;
  if(!wjoin(target,directory,data.name))continue;
  if(data.attributes&0x10){scan(target,depth+1,on);continue;}
  if(!on||!extension(data.name)||data.sizehigh||data.sizelow<64||data.sizelow>8388608||exes>=256||totalbytes>67108864-data.sizelow)continue;
  exes++;totalbytes+=data.sizelow;H f=CreateFileW(target,0x80000000,1,0,3,0x80,0);if(f==(H)-1)continue;U32 high=0,n=filesize(f,&high),done=0;U8 *image=(!high&&n==data.sizelow)?allocate(0,n,0x3000,4):0;int ok=image!=0;
  while(ok&&done<n){U32 part=n-done,got=0;if(part>32768)part=32768;ok=ReadFile(f,image+done,part,&got,0)&&got>0&&got<=part;done+=got;}CloseHandle(f);
  int profile=ok?p2d_pe_profile(image,n):0;if(image)release(image,0,0x8000);if(profile<=0)continue;found++;
  W dll[512];if((profile&1)&&wjoin(dll,directory,L"libGLES_CL.dll"))result(p2d_install(&io,dll,ROOT L"libGLES_CL.dll"));
  if((profile&2)&&wjoin(dll,directory,L"libGLES_CM.dll"))result(p2d_install(&io,dll,ROOT L"libGLES_CM.dll"));
  if((profile&1)&&wequal(data.name,L"cod2_gles.exe"))shortcut(target);
 }while(findnext(search,&data));findclose(search);
}
static void lognumber(H f,U32 n){char b[11];U32 pos=10;b[pos--]='\n';do{b[pos--]='0'+n%10;n/=10;}while(n&&pos);U32 wrote;WriteFile(f,b+pos+1,10-pos,&wrote,0);}
static void logscan(int on){H f=CreateFileW(ROOT L"setup.log",0x40000000,3,0,2,0x80,0);if(f==(H)-1)return;const char *s="P2DGLES setup v1: mode, entries, dirs, exes, supported, applied, conflicts, failures\n";U32 n=0,wrote;while(s[n])n++;WriteFile(f,s,n,&wrote,0);lognumber(f,on);lognumber(f,entries);lognumber(f,dirs);lognumber(f,exes);lognumber(f,found);lognumber(f,applied);lognumber(f,conflicts);lognumber(f,failures);CloseHandle(f);}
void _start(void){
 core=LoadLibraryW(L"coredll.dll");if(!core)ExitThread(1);
#define LOAD(v,n) v=(void*)GetProcAddressW(core,L##n)
 LOAD(findfirst,"FindFirstFileW");LOAD(findnext,"FindNextFileW");LOAD(findclose,"FindClose");LOAD(copyfile,"CopyFileW");LOAD(movefile,"MoveFileW");LOAD(deletefile,"DeleteFileW");LOAD(attributes,"GetFileAttributesW");LOAD(setattributes,"SetFileAttributesW");LOAD(geterror,"GetLastError");LOAD(filesize,"GetFileSize");LOAD(flushfile,"FlushFileBuffers");LOAD(allocate,"VirtualAlloc");LOAD(release,"VirtualFree");LOAD(createmutex,"CreateMutexW");
 if(!findfirst||!findnext||!findclose||!copyfile||!movefile||!deletefile||!attributes||!setattributes||!geterror||!filesize||!flushfile||!allocate||!release||!createmutex)ExitThread(2);
 H mutex=createmutex(0,0,L"P2DGLES_Setup_v1");if(!mutex||geterror()==183)ExitThread(0);
 P2DIO ops={0,hfile,rfile,putfile,cfile,mfile,dfile,writable};io=ops;
 for(;;){int on=mode();if(on<0)break;entries=dirs=exes=totalbytes=found=applied=conflicts=failures=0;scan(L"\\Program Files",0,on);scan(L"\\Storage Card",0,on);if(!on)result(p2d_restore(&io,SHORTCUT));logscan(on);Sleep(60000);}
 CloseHandle(mutex);ExitThread(0);
}
