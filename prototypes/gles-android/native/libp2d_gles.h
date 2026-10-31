#ifndef P2D_GLES_NATIVE_H
#define P2D_GLES_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native renderer selector, single context, sequential calls only.
 * GPU preferred; P2D_GLES_BACKEND=cpu selects the accepted software renderer.
 * GPU initialization failure falls back to CPU before any commands execute.
 * Execution failure never changes backend within an established context.
 * NULL means initialization failed (including enabled POCKETHLE_DUMP_TEXTURES).
 * The engine must destroy/recreate the context on machine/session reset. */
void *p2d_gles_create(void);
/* NULL is a no-op. All other handles must be live create() results. */
void p2d_gles_destroy(void *context);
/* Return 0: complete response; 1: complete protocol/renderer failure response.
 * Both nonnegative statuses publish the returned packet. Negative: no response;
 * -1 invalid ABI argument, -2 caught renderer panic/poisoned context (destroy it).
 * Output storage belongs to context until its next execute() or destroy().
 * Bounds: request 24..16777216 bytes, response at most 8388708 bytes.
 * A terminal float-query record adds a 76-byte trailer after the unchanged
 * framebuffer: magic 0x31514750, pname, count (1..16), sixteen float words.
 * Non-null pointers must refer to accessible buffers/output slots for the stated
 * lengths. Output slots must be aligned, distinct, and must not overlap request,
 * context, or previous response storage. Do not pass the previous response back
 * as a request. Arbitrary dangling non-null pointers are a caller error.
 * Failure packets contain the current framebuffer and preserve request sequence;
 * dimensions can differ from a rejected request. Never treat status 1 as EOF.
 * Panic unwinding is caught; allocation failure/OS termination is not catchable. */
int p2d_gles_execute(void *context, const uint8_t *request, size_t request_size,
                     const uint8_t **response, size_t *response_size);
#ifdef __cplusplus
}
#endif
#endif
