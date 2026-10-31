/* East Story compatibility: recognize caller-provided shader inputs by size
 * and SHA-256 only. No game shader binaries or original shader text are bundled.
 * The replacement GLSL below is authored for the required transform, texturing
 * and vertex-color operations; interface names match the calling application. */
#define P2D2_EAST_SHADER_MAP 1
#include "shader_fingerprint.h"
typedef struct {U32 size,type;unsigned char digest[32];const char *source;} EastShader;
static const char east_textured_vertex_source[]="#ifdef GL_ES\nprecision lowp float;\n#endif\nattribute vec2 vertexPos;\nattribute vec2 vertexTc;\nattribute vec4 vertexColor;\nuniform mat4 mvp;\nvarying vec2 texC;\nvarying vec4 clr;\nvoid main(void) {\n vec4 position = vec4(vertexPos.x, vertexPos.y, 0.0, 1.0);\n gl_Position = mvp * position;\n texC = vertexTc;\n clr = vertexColor;\n}\n";
static const char east_textured_fragment_source[]="#ifdef GL_ES\nprecision highp float;\n#endif\nuniform sampler2D TexImages0;\nvarying vec2 texC;\nvarying vec4 clr;\nvoid main(void) {\n vec4 sampled = texture2D(TexImages0, texC);\n gl_FragColor = clr * sampled;\n}\n";
static const char east_color_vertex_source[]="#ifdef GL_ES\nprecision lowp float;\n#endif\nattribute vec2 vertexPos;\nattribute vec4 vertexColor;\nuniform mat4 mvp;\nvarying vec4 clr;\nvoid main(void) {\n vec4 position = vec4(vertexPos.x, vertexPos.y, 0.0, 1.0);\n gl_Position = mvp * position;\n clr = vertexColor;\n}\n";
static const char east_color_fragment_source[]="#ifdef GL_ES\nprecision highp float;\n#endif\nvarying vec4 clr;\nvoid main(void) {\n vec4 outputColor = clr;\n gl_FragColor = outputColor;\n}\n";
static const EastShader east_shaders[]={
 {620,35633,{0xc1,0x40,0xe5,0x0c,0xbf,0x97,0x77,0x71,0x96,0x0c,0xa9,0x94,0x1b,0x4f,0x0c,0xc1,0xf3,0xaa,0x6b,0x98,0x6c,0x63,0x9c,0xad,0x54,0x33,0xc0,0x58,0x06,0xda,0x2a,0x61},east_textured_vertex_source},
 {280,35632,{0x10,0x4e,0x3b,0x86,0x87,0xc6,0x75,0xf4,0x32,0x58,0xbf,0xfc,0x2f,0x00,0x04,0x63,0xdc,0x0b,0x7c,0xc1,0x16,0x0a,0xb6,0x42,0xc0,0xf0,0x93,0xac,0xf1,0x6c,0xad,0xa0},east_textured_fragment_source},
 {536,35633,{0x63,0xa4,0xd5,0x0d,0xd5,0xba,0x45,0xa8,0xe0,0x90,0x85,0xb2,0x63,0x31,0x51,0x35,0xd0,0x2d,0xc9,0x5d,0x35,0x88,0xb8,0xbf,0x7d,0xc3,0xe5,0x8e,0x55,0x8f,0x06,0xf2},east_color_vertex_source},
 {176,35632,{0x75,0x00,0x96,0x87,0x19,0x77,0x42,0x86,0xe2,0x46,0x1c,0x65,0xd0,0xa2,0xce,0x23,0x0f,0xf2,0x0d,0x28,0x18,0x44,0x8d,0x82,0xa1,0xba,0x90,0x17,0x70,0x08,0xa5,0xa4},east_color_fragment_source},
};
static const EastShader *east_shader_find(const unsigned char *binary,U32 size){
 unsigned char digest[32];
 for(U32 i=0;i<4;i++){
  const EastShader *shader=east_shaders+i;
  if(shader->size!=size)continue;
  p2d_shader_sha256(binary,size,digest);
  U32 j=0;while(j<32&&digest[j]==shader->digest[j])j++;
  if(j==32)return shader;
 }
 return 0;
}
static U32 east_shader_binary(U32 *a){
 if((I32)a[0]<0||(I32)a[4]<0){fail(0x501);return 0;}
 if(a[2]){fail(0x500);return 0;}
 if(a[0]!=1||!a[1]||!a[3]){fail(0x501);return 0;}
 const EastShader *shader=east_shader_find((const unsigned char *)(uintptr_t)a[3],a[4]);
 if(!shader){fail(0x501);return 0;}
 U32 object=*(U32 *)(uintptr_t)a[1],args[16]={object,0x8b4f},type=0;
 if(!query(P2D2_glGetShaderiv,args,0,0,&type,4))return 0;
 if(type!=shader->type){fail(0x501);return 0;}
 U32 length=0;while(shader->source[length])length++;args[1]=1;
 if(!record(P2D2_glShaderSource,args,shader->source,length))return 0;
 record(P2D2_glCompileShader,args,0,0);return 0;
}
