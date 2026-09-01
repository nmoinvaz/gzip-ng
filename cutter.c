/* cutter.c -- cutting candidate segments out of the compressed input
 * For conditions of distribution and use, see LICENSE.md
 */

#include "cutter.h"

#include <stdlib.h>

#include "gzblock.h"
#include "scanner.h"
#include "util.h"

void cutter_init(cutter_t *cutter, uint32_t block_size, int32_t paired) {
    cutter->block_size = block_size;
    /* A pair-terminated block may be any size and coalescing gathers several, so this is a
       memory bound rather than a property of the format. */
    cutter->max_seg = (size_t)block_size * 4 + 1024;
    cutter->paired = paired;
    cutter->pair_seen = 0;
    cutter->scanned = 0;
    cutter->coal = 0;
}

/* Move the first n bytes of the input into seg. */
static int32_t cutter_cut(cutter_t *cutter, buf_t *buf, size_t n, int32_t last, int32_t pair) {
    cutter->seg.len = 0;
    if (buf_append(&cutter->seg, buf_data(buf), n) != 0)
        return CUT_ERROR;
    cutter->seg_last = last;
    cutter->seg_pair = pair;
    buf_drop(buf, n);
    cutter->scanned = 0;
    cutter->coal = 0;
    return CUT_FOUND;
}

/* The blocks are larger than assumed, which happens when the probe's guess was low. A
   pair-terminated block is valid at any size, so raise the bound and rescan rather than give up.
   Returns 0 once the bound cannot grow. */
static int32_t cutter_widen(cutter_t *cutter) {
    if (!cutter->pair_seen || cutter->max_seg >= GZBLOCK_MAX_BLOCK)
        return 0;
    cutter->max_seg = MIN(cutter->max_seg * 2, (size_t)GZBLOCK_MAX_BLOCK);
    return 1;
}

int32_t cutter_next(cutter_t *cutter, buf_t *buf, int32_t eof) {
    for (;;) {
        size_t limit = buf->len >= 3 ? buf->len - 3 : 0;
        const uint8_t *bp = buf_data(buf);
        const uint8_t *hit = cutter->scanned < limit ? scan_marker(bp + cutter->scanned, bp + limit) : NULL;
        if (hit) {
            size_t n = (size_t)(hit - bp) + 4;
            int32_t empties = 0;
            /* Empty stored blocks right after the marker belong to this segment too, the second one
               is what makes a boundary when the header says pairs. Their bytes must be in hand. */
            for (;;) {
                if (n + 5 > buf->len) {
                    if (eof)
                        break;
                    cutter->scanned = (size_t)(hit - bp);
                    return CUT_MORE;
                }
                if (!scan_empty_block(bp + n))
                    break;
                n += 5;
                empties++;
            }
            if (empties > 0)
                cutter->pair_seen = 1;
            if ((cutter->paired || cutter->pair_seen) && empties == 0) {
                /* a lone marker, a flush inside a block or data that happens to match */
                cutter->scanned = (size_t)(hit - bp) + 1;
                continue;
            }
            if (empties > 0 && n < cutter->block_size) {
                /* A small pair-terminated chunk. One slot per chunk costs more in handoff than
                   inflation, so keep absorbing chunks until a block of input is in hand. */
                cutter->coal = n;
                cutter->scanned = n;
                continue;
            }
            if (n > cutter->max_seg) {
                if (cutter->coal != 0)
                    return cutter_cut(cutter, buf, cutter->coal, 0, 1);
                if (cutter_widen(cutter))
                    continue;
                return CUT_TOO_LARGE;
            }
            return cutter_cut(cutter, buf, n, 0, empties > 0);
        }
        cutter->scanned = limit;
        if (buf->len > cutter->max_seg + 3) {
            if (cutter->coal != 0)
                return cutter_cut(cutter, buf, cutter->coal, 0, 1);
            if (cutter_widen(cutter))
                continue;
            return CUT_TOO_LARGE;
        }
        if (eof) {
            if (buf->len == 0)
                return CUT_DONE;
            return cutter_cut(cutter, buf, buf->len, 1, 0);
        }
        return CUT_MORE;
    }
}

void cutter_rescan(cutter_t *cutter, size_t from) {
    cutter->scanned = from;
    cutter->coal = 0;
}

void cutter_free(cutter_t *cutter) {
    free(cutter->seg.p);
}
