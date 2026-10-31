/* Proof only for successfully queued, valid GLES state setters. No defaults
 * assumed; unit-scoped proofs require a successfully queued selector too. */
#ifndef P2D_GUEST_COMMAND_CACHE_H
#define P2D_GUEST_COMMAND_CACHE_H
typedef struct {unsigned int valid,value[14];} P2DCommandCache;
static void p2d_command_invalidate(P2DCommandCache*c){c->valid=0;}
static int p2d_command_slot(const P2DCommandCache*c,unsigned int op,unsigned int arg,
                           unsigned int units,unsigned int client,unsigned int server){
 switch(op){
 case OP_glActiveTexture:return arg>=0x84c0&&arg-0x84c0<units?0:-1;
 case OP_glClientActiveTexture:return arg>=0x84c0&&arg-0x84c0<units?1:-1;
 case OP_glMatrixMode:return arg==0x1700||arg==0x1701||arg==0x1702?2:-1;
 case OP_glEnableClientState:case OP_glDisableClientState:
  if(arg==0x8074)return 3;if(arg==0x8075)return 4;if(arg==0x8076)return 5;
  if(arg==0x8078&&client<units&&client<4&&(c->valid&2u)&&c->value[1]==0x84c0+client)return 6+(int)client;
  return -1;
 case OP_glEnable:case OP_glDisable:
  if(arg==0xde1&&server<units&&server<4&&(c->valid&1u)&&c->value[0]==0x84c0+server)return 10+(int)server;
  return -1;
 default:return -1;
 }
}
static unsigned int p2d_command_value(unsigned int op,unsigned int arg){
 return op==OP_glEnableClientState||op==OP_glEnable?1u:
        op==OP_glDisableClientState||op==OP_glDisable?0u:arg;
}
static int p2d_command_repeated(const P2DCommandCache*c,unsigned int op,unsigned int arg,
                               unsigned int units,unsigned int client,unsigned int server){
 int slot=p2d_command_slot(c,op,arg,units,client,server);
 return slot>=0&&(c->valid&(1u<<slot))&&c->value[slot]==p2d_command_value(op,arg);
}
static void p2d_command_commit(P2DCommandCache*c,unsigned int op,unsigned int arg,
                              unsigned int units,unsigned int client,unsigned int server){
 /* Recompute after record(): a capacity flush may have invalidated selectors. */
 int slot=p2d_command_slot(c,op,arg,units,client,server);
 if(slot>=0){c->value[slot]=p2d_command_value(op,arg);c->valid|=1u<<slot;}
}
#endif
