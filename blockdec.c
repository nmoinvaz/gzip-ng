/* blockdec.c -- incremental decoder for one independent block
 * For conditions of distribution and use, see LICENSE.md
 */

#include "blockdec.h"
#include "util.h"

void blockdec_begin(block_dec *d, zng_stream *z, uint8_t *out, uint32_t block_size) {
    d->z = z;
    d->want_marker = 0;
    d->accept_partial = 0;
    zng_inflateReset(z);
    z->next_in = NULL;
    z->avail_in = 0;
    z->next_out = out;
    z->avail_out = block_size;
}

/* Feed the next piece of a block. Returns SEG_SHORT when the block needs more input, SEG_FULL once the
   block's output and its trailing marker are consumed, SEG_END when the deflate stream ends,
   SEG_OVERFLOW when the block wants more than block_size bytes of output, or SEG_ERROR on invalid
   data. *used receives how much of this piece was consumed. */
int blockdec_feed(block_dec *d, const uint8_t *in, size_t in_len, size_t *used) {
    zng_stream *z = d->z;
    size_t left = in_len, start_in = (size_t)z->total_in;
    int err, boundary, aligned, exhausted, status;

    z->next_in = (z_const uint8_t *)in;
    z->avail_in = 0;
    for (;;) {
        if (z->avail_in == 0 && left != 0) {
            uint32_t chunk = clamp_u32(left);
            z->avail_in = chunk;
            left -= chunk;
        }
        /* Z_BLOCK returns at every block boundary, where data_type reports the position. */
        err = zng_inflate(z, Z_BLOCK);
        if (err == Z_OK && (z->data_type & (64 | 128)) == (64 | 128))
            err = zng_inflate(z, Z_BLOCK);   /* past the final block, conclude the stream */
        if (err == Z_STREAM_END) {
            status = SEG_END;
            break;
        }
        if (err != Z_OK) {
            status = SEG_ERROR;
            break;
        }
        boundary = (z->data_type & 128) != 0;   /* just finished a deflate block */
        aligned = (z->data_type & 7) == 0;      /* and landed on a byte boundary */
        exhausted = (z->avail_in == 0 && left == 0);

        if (d->want_marker) {
            /* Only empty stored blocks may follow a full block, one from a full flush, two when pigz -i
               wrote it. */
            if (!boundary && z->avail_in == 0) {
                if (exhausted) {
                    status = SEG_SHORT;     /* the marker continues in the next piece */
                    break;
                }
                continue;
            }
            if (!(boundary && aligned)) {
                status = SEG_OVERFLOW;
                break;
            }
            if (exhausted) {
                status = SEG_FULL;
                break;
            }
            continue;                       /* another empty stored block */
        }
        if (z->avail_out != 0) {
            if (exhausted) {
                /* A segment that ends at a marker pair is a block at whatever size it produced,
                   pairs do not occur by accident. Lone markers must land exactly on block_size. */
                status = (d->accept_partial && boundary && aligned) ? SEG_FULL : SEG_SHORT;
                break;
            }
            continue;       /* more deflate blocks to go */
        }
        /* The output is full. */
        if (!boundary) {
            status = exhausted ? SEG_SHORT : SEG_OVERFLOW;
            break;
        }
        if (exhausted) {
            status = aligned ? SEG_FULL : SEG_SHORT;
            break;
        }
        d->want_marker = 1;
    }
    *used = (size_t)z->total_in - start_in;
    return status;
}

const char *blockdec_status_name(int status) {
    switch (status) {
    case SEG_FULL:     return "complete";
    case SEG_END:      return "end of stream";
    case SEG_SHORT:    return "truncated";
    case SEG_OVERFLOW: return "larger than the block size";
    default:           return "corrupt";
    }
}
