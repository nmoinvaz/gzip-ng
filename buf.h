/* buf.h -- growable byte buffer consumed from the front by offset
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_BUF_H_
#define GZNG_BUF_H_

#include <stddef.h>
#include <stdint.h>

typedef size_t (*buf_read_fn)(void *ctx, uint8_t *buf, size_t len);

/* The live data is buf_data(m), len bytes at p + off, dropping data moves the offset and the
   bytes move only when the space behind the offset is needed for a refill or an append. */
typedef struct {
    uint8_t *p;
    size_t len, cap;
    size_t off;
} membuf;

#define buf_data(m) ((m)->p + (m)->off)

int buf_reserve(membuf *m, size_t need);
int buf_append(membuf *m, const uint8_t *data, size_t n);
void buf_drop(membuf *m, size_t n);
int buf_fill(membuf *m, buf_read_fn read, void *ctx, size_t want, int *eof);

#endif /* GZNG_BUF_H_ */
