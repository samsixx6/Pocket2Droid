#ifndef P2D_GLES_SESSION_H
#define P2D_GLES_SESSION_H
typedef struct {void *ctx;void *(*create)(void*,int*);void (*close)(void*,void*);} P2DSessionOps;
/* Never wait or recursively acquire an existing owner's kernel object. */
void *p2d_claim_session(const P2DSessionOps *);
#endif
