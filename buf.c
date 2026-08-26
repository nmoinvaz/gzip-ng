/* buf.c -- growable byte buffer consumed from the front by offset
 * For conditions of distribution and use, see LICENSE.md
 */

#include "buf.h"

#include <stdlib.h>
#include <string.h>

/* Move the live bytes back to the front of the allocation. */
static void buf_compact(membuf *m) {
    if (m->off != 0) {
        memmove(m->p, m->p + m->off, m->len);
        m->off = 0;
    }
}

int buf_reserve(membuf *m, size_t need) {
    if (m->off + need > m->cap)
        buf_compact(m);
    if (need > m->cap) {
        size_t ncap = m->cap ? m->cap : (1 << 16);
        uint8_t *grown;
        while (ncap < need)
            ncap *= 2;
        grown = (uint8_t *)realloc(m->p, ncap);
        if (grown == NULL)
            return -1;
        m->p = grown;
        m->cap = ncap;
    }
    return 0;
}

int buf_append(membuf *m, const uint8_t *data, size_t n) {
    if (buf_reserve(m, m->len + n) != 0)
        return -1;
    memcpy(buf_data(m) + m->len, data, n);
    m->len += n;
    return 0;
}

void buf_drop(membuf *m, size_t n) {
    m->off += n;
    m->len -= n;
    if (m->len == 0)
        m->off = 0;
}

/* Read through the callback until the buffer holds at least want bytes or the input ends, which
   sets *eof. Returns -1 on a read error. */
int buf_fill(membuf *m, buf_read_fn read, void *ctx, size_t want, int *eof) {
    while (m->len < want && !*eof) {
        size_t got;
        if (m->off + m->len == m->cap) {
            if (m->off != 0)
                buf_compact(m);
            else if (buf_reserve(m, m->cap + 1) != 0)
                return -1;
        }
        got = read(ctx, buf_data(m) + m->len, m->cap - m->off - m->len);
        if (got == (size_t)-1)
            return -1;
        if (got == 0) {
            *eof = 1;
            break;
        }
        m->len += got;
    }
    return 0;
}
