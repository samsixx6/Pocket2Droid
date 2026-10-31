#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include "setup_core.h"
#include "session.h"
static char pathbuf[3][1024];static unsigned pathslot;
static const char *path(const W *p){char *d=pathbuf[pathslot++%3];unsigned i=0;while(p[i]&&i<1023){d[i]=(char)p[i];i++;}d[i]=0;return d;}
static int h(void *c,const W *p,U8 out[32]){(void)c;FILE *f=fopen(path(p),"rb");if(!f)return access(path(p),F_OK)?1:-1;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<0||n>64*1024*1024){fclose(f);return -1;}void *b=malloc(n?n:1);int ok=fread(b,1,n,f)==(size_t)n;fclose(f);if(ok)p2d_sha256(b,n,out);free(b);return ok?0:-1;}
static int r(void*c,const W*p,void*b,U32 n){(void)c;FILE*f=fopen(path(p),"rb");if(!f)return -1;int ok=fread(b,1,n,f)==n&&fgetc(f)==EOF;fclose(f);return ok?0:-1;}
static int put(void*c,const W*p,const void*b,U32 n){(void)c;int fd=open(path(p),O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return -1;int ok=write(fd,b,n)==n;fsync(fd);close(fd);return ok?0:-1;}
static int copy(void*c,const W*s,const W*d){(void)c;FILE*f=fopen(path(s),"rb");if(!f)return -1;int fd=open(path(d),O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0){fclose(f);return -1;}U8 b[4096];size_t n;int ok=1;while((n=fread(b,1,sizeof(b),f))){if(write(fd,b,n)!=(ssize_t)n){ok=0;break;}}if(ferror(f))ok=0;struct stat st;if(fstat(fileno(f),&st)||fchmod(fd,st.st_mode&0777))ok=0;fclose(f);fsync(fd);close(fd);return ok?0:-1;}
static int move(void*c,const W*s,const W*d){(void)c;char a[1024];strcpy(a,path(s));if(link(a,path(d)))return -1;return unlink(a);}
static int rem(void*c,const W*p){(void)c;struct stat st;if(stat(path(p),&st)||!(st.st_mode&0222))return -1;return unlink(path(p));}
static int writable(void*c,const W*p){(void)c;struct stat st;if(stat(path(p),&st))return -1;return chmod(path(p),st.st_mode|0200);}
static void *session_create(void*c,int*exists){const char*name=c;struct sockaddr_un addr;memset(&addr,0,sizeof(addr));addr.sun_family=AF_UNIX;if(strlen(name)>sizeof(addr.sun_path)-2)return 0;strcpy(addr.sun_path+1,name);int fd=socket(AF_UNIX,SOCK_DGRAM,0);if(fd<0)return 0;if(bind(fd,(struct sockaddr*)&addr,offsetof(struct sockaddr_un,sun_path)+1+strlen(name))){if(errno!=EADDRINUSE){close(fd);return 0;}*exists=1;}return(void*)(uintptr_t)(fd+1);}
static void session_close(void*c,void*token){(void)c;close((int)(uintptr_t)token-1);}
int main(int argc,char **argv){if(argc<3)return 2;W a[1024],b[1024];unsigned i;for(i=0;argv[2][i]&&i<1023;i++)a[i]=(U8)argv[2][i];a[i]=0;P2DIO io={0,h,r,put,copy,move,rem,writable};int result;
if(!strcmp(argv[1],"session")){P2DSessionOps ops={argv[2],session_create,session_close};void*token=p2d_claim_session(&ops);printf("%d\n",token?1:0);fflush(stdout);if(token){if(argc==4)getchar();session_close(0,token);}return 0;}
if(!strcmp(argv[1],"sha")){U8 out[32];if(h(0,a,out))return 3;for(i=0;i<32;i++)printf("%02x",out[i]);printf("\n");return 0;}
if(!strcmp(argv[1],"blob")){result=p2d_install_blob(&io,a,"literal shortcut",16);}
else if(!strcmp(argv[1],"scan")){FILE*f=fopen(argv[2],"rb");if(!f)return 3;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);U8*p=malloc(n?n:1);if(fread(p,1,n,f)!=(size_t)n)return 4;fclose(f);result=p2d_pe_profile(p,n);free(p);}
else if(!strcmp(argv[1],"on")&&argc==4){for(i=0;argv[3][i]&&i<1023;i++)b[i]=(U8)argv[3][i];b[i]=0;result=p2d_install(&io,a,b);}
else if(!strcmp(argv[1],"off"))result=p2d_restore(&io,a);else return 2;printf("%d\n",result);return 0;}
