#ifndef P2D2_APP_PROFILES_H
#define P2D2_APP_PROFILES_H
#include <stdint.h>
#define P2D2_APP_PROFILE 0x2ff00u
#define P2D2_PROFILE_LEGACY_STATE 1u
#define P2D2_PROFILE_EAST 2u
static inline int p2d2_profile_name(const uint16_t*p,const char*name){
 for(unsigned i=0;;i++){unsigned a=p[i],b=(unsigned char)name[i];if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;if(a!=b)return 0;if(!a)return 1;}
}
static inline uint32_t p2d2_profile_path(const uint16_t*p){
 if(!p)return 0;unsigned n=0,start=0;for(;n<260&&p[n];n++)if(p[n]=='\\'||p[n]=='/')start=n+1;
 if(!n||n==260)return 0;
 if(p2d2_profile_name(p+start,"EastStory.exe"))return 3;
 if(p2d2_profile_name(p+start,"FGTH_FerrariGT.exe"))return 1;
 return 0;
}
#endif
