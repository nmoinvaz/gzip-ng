/* decoder.c -- incremental decoder for one independent block
 * For conditions of distribution and use, see LICENSE.md
 */

#include "decoder.h"
#include "util.h"

void decoder_begin(decoder *d, zng_stream *strm, uint8_t *out, uint32_t block_size) {
    d->strm = strm;
    d->want_marker = 0;
    d->accept_partial = 0;
    zng_inflateReset(strm);
    strm->next_in = NULL;
    strm->avail_in = 0;
    strm->next_out = out;
    strm->avail_out = block_size;
}

/* Feed the next piece of a block. Returns SEG_SHORT when the block needs more input, SEG_FULL
   once the block's output and its trailing marker are consumed, SEG_END when the deflate stream
   ends, SEG_OVERFLOW when the block wants more than block_size bytes of output, or SEG_ERROR on
   invalid data. With accept_partial the input ends at a marker pair, which no chance pattern
   produces, so any clean output size ends the block and SEG_FULL comes back early. *used
   receives how much of this piece was consumed. */
int32_t decoder_feed(decoder *d, const uint8_t *in, size_t in_len, size_t *used) {
    zng_stream *strm = d->strm;
    size_t left = in_len, start_in = (size_t)strm->total_in;
    int32_t err, boundary, aligned, exhausted, status;

    strm->next_in = (z_const uint8_t *)in;
    strm->avail_in = 0;
    for (;;) {
        if (strm->avail_in == 0 && left != 0) {
            uint32_t chunk = clamp_u32(left);
            strm->avail_in = chunk;
            left -= chunk;
        }
        /* Z_BLOCK returns at every block boundary, where data_type reports the position. */
        err = zng_inflate(strm, Z_BLOCK);
        if (err == Z_OK && (strm->data_type & (64 | 128)) == (64 | 128))
            err = zng_inflate(strm, Z_BLOCK); /* past the final block, conclude the stream */
        if (err == Z_STREAM_END) {
            status = SEG_END;
            break;
        }
        if (err != Z_OK) {
            status = SEG_ERROR;
            break;
        }
        boundary = (strm->data_type & 128) != 0; /* just finished a deflate block */
        aligned = (strm->data_type & 7) == 0;    /* and landed on a byte boundary */
        exhausted = (strm->avail_in == 0 && left == 0);

        if (d->want_marker) {
            /* Only empty stored blocks may follow a full block, one from a full flush, two when
               pigz --independent wrote it. */
            if (!boundary && strm->avail_in == 0) {
                if (exhausted) {
                    status = SEG_SHORT; /* the marker continues in the next piece */
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
            continue; /* another empty stored block */
        }
        if (strm->avail_out != 0) {
            if (exhausted) {
                /* A segment that ends at a marker pair is a block at whatever size it produced,
                   pairs do not occur by accident. Lone markers must land exactly on block_size. */
                status = (d->accept_partial && boundary && aligned) ? SEG_FULL : SEG_SHORT;
                break;
            }
            continue; /* more deflate blocks to go */
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
    *used = (size_t)strm->total_in - start_in;
    return status;
}

const char *decoder_status_name(int32_t status) {
    switch (status) {
    case SEG_FULL:
        return "complete";
    case SEG_END:
        return "end of stream";
    case SEG_SHORT:
        return "truncated";
    case SEG_OVERFLOW:
        return "larger than the block size";
    default:
        return "corrupt";
    }
}
