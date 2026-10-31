#ifndef P2D_GLES_SETUP_CORE_H
#define P2D_GLES_SETUP_CORE_H
typedef unsigned char U8;
typedef unsigned int U32;
typedef unsigned short W;
typedef struct {
    void *ctx;
    int (*hash)(void *,const W *,U8[32]); /* 0 found,1 absent,-1 failure */
    int (*read)(void *,const W *,void *,U32); /* exact size only */
    int (*putnew)(void *,const W *,const void *,U32);
    int (*copynew)(void *,const W *,const W *);
    int (*movenew)(void *,const W *,const W *);
    int (*remove)(void *,const W *);
    int (*writable)(void *,const W *); /* only after installed hash matches */
} P2DIO;
int p2d_pe_profile(const U8 *,U32);
int p2d_install(const P2DIO *,const W *,const W *);
int p2d_install_blob(const P2DIO *,const W *,const void *,U32);
int p2d_restore(const P2DIO *,const W *);
void p2d_sha256(const void *,U32,U8[32]);
typedef struct {U32 state[8],length,used;U8 block[64];} P2DHash;
void p2d_hash_start(P2DHash *);
void p2d_hash_update(P2DHash *,const void *,U32);
void p2d_hash_end(P2DHash *,U8[32]);
#endif
