#define P2D2_LEGACY_CONFIG_ATTRIBUTES 1
#include "internal.h"
static U32 bad(U32 e){S.eglerror=e;return 0;}
/* WinCE EGLNativeDisplayType is a native display DC, in addition to zero.
   Validate through GDI; arbitrary integers and non-DC GDI objects stay invalid. */
static U32 native_display(U32 handle){
 if(!handle)return 1;
 static const W name[]={'G','e','t','O','b','j','e','c','t','T','y','p','e',0};
 U32(*kind)(H)=(void*)symbol(name);
 return kind&&kind((H)(uintptr_t)handle)==3?1:bad(0x300c); /* OBJ_DC */
}
static int attr(U32 n,U32*out){switch(n){case 0x3020:*out=32;break;case 0x3021:case 0x3022:case 0x3023:case 0x3024:*out=8;break;case 0x3025:*out=24;break;case 0x3026:*out=8;break;case 0x3027:*out=0x3038;break;case 0x3028:*out=1;break;case 0x3029:*out=0;break;case 0x302a:*out=0;break;case 0x302b:*out=2048*2048;break;case 0x302c:case 0x302d:*out=2048;break;case 0x3031:case 0x3032:*out=0;break;case 0x3033:*out=4;break;case 0x3034:*out=0x3038;break;case 0x303f:*out=0x308e;break;case 0x3040:case 0x3042:*out=4;break;case 0x3037:case 0x3035:case 0x3036:*out=0;break;default:return 0;}return 1;}
static int configs(const U32*p){if(!p)return 1;int match=1;for(U32 i=0;i<128;i+=2){U32 key=p[i];if(key==0x3038)return match;U32 want=p[i+1],have;if(!attr(key,&have))return -1;if(want==0xffffffff)continue;if(key==0x3033||key==0x3040||key==0x3042){if((have&want)!=want)match=0;}else if(key==0x3027||key==0x3028||key==0x3034||key==0x303f){if(have!=want)match=0;}else if((I32)want<0||have<want)match=0;}return -1;}
/* Gameera's older SDK reuses the choose-config list for object creation.
   Only the private compatibility build accepts redundant, satisfiable config
   fields. Normal builds retain EGL_BAD_ATTRIBUTE for these surface attributes. */
#ifdef P2D2_LEGACY_CONFIG_ATTRIBUTES
static int reused_config_attributes(const U32*p,U32 context){
 U32 es2=0;
 for(U32 i=0;i<128;i+=2){
  U32 k=p[i];if(k==0x3038)return i&&(!context||es2)&&configs(p)==1;
  if(k!=0x3024&&k!=0x3023&&k!=0x3022&&k!=0x3021&&k!=0x3025&&k!=0x3026&&k!=0x3040)return 0;
  if(k==0x3040&&p[i+1]==4)es2=1;
 }
 return 0;
}
#endif
static int attributes(const U32*p,U32 context){
 if(!p)return 1;
#ifdef P2D2_LEGACY_CONFIG_ATTRIBUTES
 if((S.compat_profile&P2D2_PROFILE_EAST)&&reused_config_attributes(p,context))return 1;
#endif
 for(U32 i=0;i<128;i+=2){if(p[i]==0x3038)return 1;if(context){if(p[i]!=0x3098||p[i+1]!=2)return 0;}else if(p[i]!=0x3086||p[i+1]!=0x3084)return 0;}return 0;
}
static int reap(void){U32 a[16]={0};if(S.context_dead){if(!record(P2D2_eglDestroyContext,a,0,0)||!flush(0))return 0;for(U32 i=0;i<1024;i++)if(S.buffers[i].data)S.release(S.buffers[i].data,0,0x8000);zero(S.buffers,sizeof(S.buffers));zero(S.attr,sizeof(S.attr));for(U32 i=0;i<16;i++){S.attr[i].size=4;S.attr[i].type=0x1406;}S.array_buffer=S.element_buffer=S.buffer_bytes=S.error=S.num_formats=S.num_shader_formats=0;S.unpack=4;S.context=0;S.context_dead=0;}if(S.surface_dead){S.surface=0;S.surface_dead=0;S.window=0;p2d_present_invalidate(&S.presentation);}return 1;}
U32 guest_egl(U32 op,U32*a){
 if(op==P2D2_eglGetError){U32 e=S.eglerror;S.eglerror=0x3000;return e;}
 if(op==P2D2_eglGetDisplay)return native_display(a[0]);
 if(op==P2D2_eglGetProcAddress){const unsigned char*p=(void*)a[0];W name[128];if(!p)return 0;U32 i=0;while(i<127&&p[i]){name[i]=p[i];i++;}if(i==127&&p[i])return 0;name[i]=0;return (U32)GetProcAddressW(S.module,name);}
 if(op==P2D2_eglGetCurrentContext)return S.current?S.context:0;
 if(op==P2D2_eglGetCurrentDisplay)return S.current?1:0;
 if(op==P2D2_eglGetCurrentSurface)return (a[0]==0x3059||a[0]==0x305a)&&S.current?S.surface:0;
 if(op==P2D2_eglBindAPI)return a[0]==0x30a0?1:bad(0x300c);
 if(op==P2D2_eglQueryAPI)return 0x30a0;
 if(op==P2D2_eglWaitGL||op==P2D2_eglWaitNative)return flush(0)?1:bad(0x3002);
 if(a[0]!=1)return bad(0x3008);
 if(op==P2D2_eglInitialize){U32 values[3]={0};if(!query(op,a,0,0,values,12)||!values[0])return bad(0x3001);S.eglinit=1;if(a[1])*(U32*)a[1]=values[1];if(a[2])*(U32*)a[2]=values[2];return 1;}
 if(!S.eglinit)return bad(0x3001);
 switch(op){
 case P2D2_eglQueryString:return (U32)(a[1]==0x3053?"Pocket2Droid GLES2":a[1]==0x3054?"1.4":a[1]==0x308d?"OpenGL_ES":"");
 case P2D2_eglChooseConfig:{if((I32)a[3]<0||!a[4])return bad(0x300c);int match=configs((void*)a[1]);if(match<0)return bad(0x3004);*(U32*)a[4]=match?1:0;if(match&&a[2]&&a[3])*(U32*)a[2]=1;return 1;}
 case P2D2_eglGetConfigs:if((I32)a[2]<0||!a[3])return bad(0x300c);*(U32*)a[3]=1;if(a[1]&&a[2])*(U32*)a[1]=1;return 1;
 case P2D2_eglGetConfigAttrib:{U32 value;if(a[1]!=1)return bad(0x3005);if(!a[3])return bad(0x300c);if(!attr(a[2],&value))return bad(0x3004);*(U32*)a[3]=value;return 1;}
 case P2D2_eglCreateWindowSurface:if(a[1]!=1)return bad(0x3005);if(!a[2])return bad(0x300b);if(S.surface)return bad(0x3003);if(!attributes((void*)a[3],0))return bad(0x3004);S.window=(H)a[2];dimensions();if(S.failed)return bad(0x3003);S.surface=2;return 2;
 case P2D2_eglCreateContext:if(a[1]!=1)return bad(0x3005);if(a[2])return bad(0x3009);if(S.context)return bad(0x3003);if(!attributes((void*)a[3],1))return bad(0x3009);S.context=3;return 3;
 case P2D2_eglMakeCurrent:if(!a[1]&&!a[2]&&!a[3]){S.current=0;return reap()?1:bad(0x3003);}if(a[1]!=S.surface||a[2]!=S.surface||a[3]!=S.context||!S.surface||!S.context||S.context_dead||S.surface_dead)return bad(0x3009);S.current=1;return 1;
 case P2D2_eglSwapInterval:return S.current?1:bad(0x3009);
 case P2D2_eglSwapBuffers:if(!S.current||a[1]!=S.surface||!S.surface)return bad(0x300d);if(!record(op,a,0,0)||!flush(0))return bad(0x3003);present();return S.failed?bad(0x3003):1;
 case P2D2_eglQuerySurface:if(a[1]!=S.surface||!S.surface||S.surface_dead)return bad(0x300d);if(!a[3])return bad(0x300c);if(a[2]!=0x3057&&a[2]!=0x3056&&a[2]!=0x3028&&a[2]!=0x3086)return bad(0x3004);*(U32*)a[3]=a[2]==0x3057?S.width:a[2]==0x3056?S.height:a[2]==0x3028?1:0x3084;return 1;
 case P2D2_eglDestroyContext:if(a[1]!=S.context||!S.context||S.context_dead)return bad(0x3006);S.context_dead=1;if(!S.current&&!reap())return bad(0x3003);return 1;
 case P2D2_eglDestroySurface:if(a[1]!=S.surface||!S.surface||S.surface_dead)return bad(0x300d);S.surface_dead=1;if(!S.current&&!reap())return bad(0x3003);return 1;
 case P2D2_eglTerminate:record(op,a,0,0);flush(0);shutdown_guest();return 1;
 default:return bad(0x300c);
 }
}
