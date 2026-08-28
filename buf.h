/* buf.h -- growable byte buffer consumed from the front by offset
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_BUF_H_
#define GZNG_BUF_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*buf_read_fn)(void *ctx, uint8_t *buf, size_t len);

/* The live data is buf_data(buf), len bytes at p + offset, dropping data moves the offset and the
   bytes move only when the space behind the offset is needed for a refill or an append. */
typedef struct {
    uint8_t *p;
    size_t len, size;
    size_t offset;
} buf_t;

#define buf_data(buf) ((buf)->p + (buf)->offset)

int buf_reserve(buf_t *buf, size_t need);
int buf_append(buf_t *buf, const uint8_t *data, size_t n);
void buf_drop(buf_t *buf, size_t n);
int buf_fill(buf_t *buf, buf_read_fn read, void *ctx, size_t want, int *eof);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_BUF_H_ */
