/* buf.c -- growable byte buffer consumed from the front by offset
 * For conditions of distribution and use, see LICENSE.md
 */

#include "buf.h"

#include <stdlib.h>
#include <string.h>

/* Move the live bytes back to the front of the allocation. */
static void buf_compact(buf_t *buf) {
    if (buf->off != 0) {
        memmove(buf->p, buf->p + buf->off, buf->len);
        buf->off = 0;
    }
}

int buf_reserve(buf_t *buf, size_t need) {
    if (buf->off + need > buf->cap)
        buf_compact(buf);
    if (need > buf->cap) {
        size_t ncap = buf->cap ? buf->cap : (1 << 16);
        uint8_t *grown;
        while (ncap < need)
            ncap *= 2;
        grown = (uint8_t *)realloc(buf->p, ncap);
        if (grown == NULL)
            return -1;
        buf->p = grown;
        buf->cap = ncap;
    }
    return 0;
}

int buf_append(buf_t *buf, const uint8_t *data, size_t n) {
    if (buf_reserve(buf, buf->len + n) != 0)
        return -1;
    memcpy(buf_data(buf) + buf->len, data, n);
    buf->len += n;
    return 0;
}

void buf_drop(buf_t *buf, size_t n) {
    buf->off += n;
    buf->len -= n;
    if (buf->len == 0)
        buf->off = 0;
}

/* Read through the callback until the buffer holds at least want bytes or the input ends, which
   sets *eof. Returns -1 on a read error. */
int buf_fill(buf_t *buf, buf_read_fn read, void *ctx, size_t want, int *eof) {
    while (buf->len < want && !*eof) {
        size_t got;
        if (buf->off + buf->len == buf->cap) {
            if (buf->off != 0)
                buf_compact(buf);
            else if (buf_reserve(buf, buf->cap + 1) != 0)
                return -1;
        }
        got = read(ctx, buf_data(buf) + buf->len, buf->cap - buf->off - buf->len);
        if (got == (size_t)-1)
            return -1;
        if (got == 0) {
            *eof = 1;
            break;
        }
        buf->len += got;
    }
    return 0;
}
