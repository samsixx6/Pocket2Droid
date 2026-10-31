#ifndef P2D2_PROTOCOL_H
#define P2D2_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#define P2D2_REQUEST_HEADER_BYTES 24u
#define P2D2_COMMAND_HEADER_BYTES 72u
#define P2D2_OPERATION_BASE 0x20000u
#define P2D2_MAX_PACKET (16u*1024*1024)
#define P2D2_MAX_RESULT (1024u*1024)
#define P2D2_MAX_BUFFER (64u*1024*1024)
typedef struct {uint32_t words[19];} P2D2Reply;
typedef struct {const uint8_t *bytes;size_t length;uint32_t seq,count,width,height;} P2D2Packet;
typedef struct {uint32_t op,args[16];const uint8_t *payload;uint32_t length;} P2D2Record;
int p2d2_packet_parse(const uint8_t*,size_t,P2D2Packet*);
int p2d2_record_at(const P2D2Packet*,uint32_t,P2D2Record*);
static inline uint32_t p2d2_u32(const void *v){const uint8_t*p=v;return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
#endif
