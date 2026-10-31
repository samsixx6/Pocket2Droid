#include "command-fixture.c"
static void call4(U32 op,U32 x,U32 y,U32 z,U32 w){U32 a[4]={x,y,z,w};bridge(op,a,0,4);}
int main(void){
 unsigned char *data=fake_alloc(0,4096,0x3000,4);float vertices[]={-1,-1,0,1,-1,0,0,1,0};memcpy(data,vertices,sizeof(vertices));
 call4(OP_glBindBuffer,0x8892,7,0,0);
 assert(!glerror&&"VBO bind must be implemented");
 call4(OP_glBufferData,0x8892,sizeof(vertices),(U32)data,0x88e4);
 call4(OP_glVertexPointer,3,0x1406,0,0);command(OP_glEnableClientState,0x8074);
 call4(OP_glWeightPointerOES,2,0x1406,44,28);call4(OP_glMatrixIndexPointerOES,2,0x1401,44,36);
 assert(!glerror&&weight.size==2&&matrix_index.size==2&&"disabled palette descriptors must not poison later buffer creation");
 call4(OP_glWeightPointerOES,5,0x1406,44,28);assert(glerror==0x501&&weight.size==2);glerror=0;
 call4(OP_glVertexPointer,1,0x1406,0,0);assert(glerror==0x501&&"invalid VBO pointer must report immediately");glerror=0;
 call4(OP_glVertexPointer,3,0x1405,0,0);assert(glerror==0x500);glerror=0;
 call4(OP_glVertexPointer,3,0x1406,0xffffffff,0);assert(glerror==0x501);glerror=0;
 assert(vertex.size==3&&vertex.type==0x1406&&!vertex.stride);
 call4(OP_glBindBuffer,0x8892,0,0,0);
 U32 before=records;call4(OP_glDrawArrays,4,0,3,0);assert(!glerror&&records>before);
 U32 *last=(U32*)((unsigned char*)packet+used-72);assert(last[0]==OP_glDrawArrays);
 /* Reallocation must use the captured object's NEW store, never freed bytes. */
 call4(OP_glBindBuffer,0x8892,7,0,0);call4(OP_glBufferData,0x8892,128,0,0x88e8);
 call4(OP_glBufferSubData,0x8892,0,sizeof(vertices),(U32)data);
 call4(OP_glDrawArrays,4,0,3,0);assert(!glerror);
 call4(OP_glBufferSubData,0x8892,120,16,(U32)data);assert(glerror==0x501);glerror=0;
 /* An element-buffer offset of zero is valid and mixed client arrays work. */
 call4(OP_glBindBuffer,0x8893,8,0,0);unsigned short indices[]={0,1,2};memcpy(data+256,indices,sizeof(indices));
 call4(OP_glBufferData,0x8893,sizeof(indices),(U32)(data+256),0x88e4);
 call4(OP_glDrawElements,4,3,0x1403,0);assert(!glerror);
 call4(OP_glDrawElements,4,4,0x1403,0);assert(glerror==0x501);glerror=0;
 U32 *value=(U32*)(data+1024);call4(OP_glGetFloatv,0x84e2,(U32)value,0,0);assert(*value==0x40000000);
 *value=0xdeadbeef;fixture_host_error=0x502;call4(OP_glGetFloatv,0xba6,(U32)value,0,0);
 assert(!failed&&glerror==0x502&&*value==0xdeadbeef&&"rejected query batch must preserve session/output");fixture_host_error=0;glerror=0;
 /* Unknown opcodes must not index beyond the supported-operation table. */
 call4(0x7fffffff,0,0,0,0);assert(glerror==0x502);glerror=0;
 U32 *names=(U32*)(data+512);names[0]=7;names[1]=8;call4(OP_glDeleteBuffers,2,(U32)names,0,0);
 assert(!glerror);call4(OP_glDrawElements,4,3,0x1403,0);assert(glerror==0x501);
 munmap(data,4096);puts("buffer capture/reallocate/subdata/offset/delete/bounds passed");return 0;
}
