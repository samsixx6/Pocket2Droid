#include "session.h"
void *p2d_claim_session(const P2DSessionOps *ops){int existing=0;void *handle=ops->create(ops->ctx,&existing);if(!handle)return 0;if(existing){ops->close(ops->ctx,handle);return 0;}return handle;}
