/* Guest-owned buffer stores lower VBO offsets into the established snapshots.
 * Captured array bindings survive later binds and store reallocation. No work
 * or allocation is added to draws that use only ordinary client arrays. */
#define P2D_BUFFER_SLOTS 256
#define P2D_BUFFER_BYTES (16u*1024*1024)
typedef struct {U32 name,size;unsigned char *data;} P2DBuffer;
static P2DBuffer buffers[P2D_BUFFER_SLOTS];
static U32 array_buffer,element_buffer,buffer_bytes,next_buffer=1;
static int (*buffer_free)(void*,U32,U32);
static P2DBuffer *buffer_find(U32 name){for(U32 i=0;i<P2D_BUFFER_SLOTS;i++)if(buffers[i].name==name&&name)return &buffers[i];return 0;}
static P2DBuffer *buffer_create(U32 name){
 P2DBuffer *b=buffer_find(name);if(b||!name)return b;
 for(U32 i=0;i<P2D_BUFFER_SLOTS;i++)if(!buffers[i].name){buffers[i].name=name;return &buffers[i];}
 error(0x505);return 0;
}
static U32 *buffer_binding(U32 target){if(target==0x8892)return &array_buffer;if(target==0x8893)return &element_buffer;error(0x500);return 0;}
static void buffer_bind(U32 target,U32 name){U32 *binding=buffer_binding(target);if(!binding)return;if(name&&!buffer_create(name))return;*binding=name;}
static void buffer_generate(U32 n,U32 *out){
 if(n>P2D_BUFFER_SLOTS||(!out&&n)){error(0x501);return;}
 U32 free=0;for(U32 i=0;i<P2D_BUFFER_SLOTS;i++)free+=!buffers[i].name;
 if(n>free){error(0x505);return;}
 for(U32 i=0;i<n;i++){while(!next_buffer||buffer_find(next_buffer))next_buffer++;out[i]=next_buffer++;buffer_create(out[i]);}
}
static void buffer_data(U32 target,U32 size,const void *data,U32 usage){
 U32 *binding=buffer_binding(target);if(!binding)return;
 if(usage!=0x88e4&&usage!=0x88e8){error(0x500);return;}
 P2DBuffer*b=buffer_find(*binding);if(!b){error(0x502);return;}
 if((I32)size<0){error(0x501);return;}
 if(size>P2D_BUFFER_BYTES-(buffer_bytes-b->size)){error(0x505);return;}
 if(!buffer_free)buffer_free=(int(*)(void*,U32,U32))sym(L"VirtualFree");
 if(!buffer_free){error(0x505);return;}
 unsigned char *p=size?alloc(0,size,0x3000,4):0;if(size&&!p){error(0x505);return;}
 if(size){if(data)bytes(p,data,size);else zero(p,size);}
 if(b->data)buffer_free(b->data,0,0x8000);
 buffer_bytes=buffer_bytes-b->size+size;b->data=p;b->size=size;
}
static void buffer_sub_data(U32 target,U32 offset,U32 size,const void*data){
 U32 *binding=buffer_binding(target);if(!binding)return;P2DBuffer*b=buffer_find(*binding);
 if(!b){error(0x502);return;}
 if((I32)offset<0||(I32)size<0||offset>b->size||size>b->size-offset||(!data&&size)){error(0x501);return;}
 if(size)bytes(b->data+offset,data,size);
}
static void buffer_delete(U32 n,const U32*names){
 if(n>P2D_BUFFER_SLOTS||(!names&&n)){error(0x501);return;}
 for(U32 i=0;i<n;i++){
  P2DBuffer*b=buffer_find(names[i]);if(!b)continue;
  Array *arrays[]={&vertex,&color,&normal,&texcoord[0],&texcoord[1],&texcoord[2],&texcoord[3],&weight,&matrix_index,&point_size};
  for(U32 j=0;j<10;j++)if(arrays[j]->buffer==b->name)arrays[j]->buffer=0;
  if(array_buffer==b->name)array_buffer=0;if(element_buffer==b->name)element_buffer=0;
  if(b->data)buffer_free(b->data,0,0x8000);buffer_bytes-=b->size;zero(b,sizeof(*b));
 }
}
