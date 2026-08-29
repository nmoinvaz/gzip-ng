/* codec.c -- the deflate and inflate the pool runs over one slot at a time
 * For conditions of distribution and use, see LICENSE.md
 */

#include "gzblock_p.h"

/* The output is full. A loose block may take any size, so give it the rest of the buffer, doubled
   up to GZBLOCK_MAX_BLOCK once used up. Returns 1 with room to inflate into. */
static int32_t grow_output(zng_stream *strm, slot_t *slot) {
    size_t have = (size_t)strm->total_out;

    if (have == slot->out_size) {
        size_t size = MIN(have * 2, (size_t)GZBLOCK_MAX_BLOCK);
        uint8_t *grown;
        /* Out of memory reads as a block too large to hold, which it is. */
        if (size <= have || !(grown = (uint8_t *)realloc(slot->out, size)))
            return 0;
        slot->out = grown;
        slot->out_size = size;
    }
    strm->next_out = slot->out + have;
    strm->avail_out = (uint32_t)(slot->out_size - have);
    return 1;
}

/* Inflate one segment into the slot's output and say how it ended. A strict block must fill
   exactly block_size. A loose one, ending at a marker pair or the end of the stream, is a block at
   whatever size it produces, since no chance pattern makes a pair, so its output grows on demand
   and any clean end of the input completes it. */
static int32_t inflate_segment(zng_stream *strm, slot_t *slot, uint32_t block_size) {
    int32_t loose = slot->pair || slot->last;
    int32_t want_marker = 0; /* output complete, the trailing empty stored block is still to come */
    size_t left = slot->in_len;
    int32_t err, boundary, aligned, exhausted;

    zng_inflateReset(strm);
    strm->next_in = (z_const uint8_t *)slot->in;
    strm->avail_in = 0;
    strm->next_out = slot->out;
    strm->avail_out = block_size; /* the buffer holds at least this much */
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
        if (err == Z_STREAM_END)
            return SEGMENT_END;
        if (err != Z_OK)
            return SEGMENT_ERROR;
        boundary = (strm->data_type & 128) != 0; /* just finished a deflate block */
        aligned = (strm->data_type & 7) == 0;    /* and landed on a byte boundary */
        exhausted = (strm->avail_in == 0 && left == 0);

        if (want_marker) {
            /* Only empty stored blocks may follow a full block, one from a full flush, two when
               pigz --independent wrote it. */
            if (!boundary && strm->avail_in == 0) {
                if (exhausted)
                    return SEGMENT_SHORT; /* cut inside the marker */
                continue;
            }
            if (!(boundary && aligned)) {
                if (exhausted || !loose || !grow_output(strm, slot))
                    return SEGMENT_OVERFLOW;
                want_marker = 0; /* a loose block carries on past block_size */
                continue;
            }
            if (exhausted)
                return SEGMENT_FULL;
            continue; /* another empty stored block */
        }
        if (strm->avail_out != 0) {
            if (!exhausted)
                continue; /* more deflate blocks to go */
            /* Strict blocks must land exactly on block_size, a loose one is done at any clean
               boundary. */
            return loose && boundary && aligned ? SEGMENT_FULL : SEGMENT_SHORT;
        }
        /* The output is full. */
        if (!boundary) {
            if (exhausted)
                return SEGMENT_SHORT;
            if (loose && grow_output(strm, slot))
                continue;
            return SEGMENT_OVERFLOW;
        }
        if (exhausted)
            return aligned ? SEGMENT_FULL : SEGMENT_SHORT;
        want_marker = 1;
    }
}

const char *segment_status_name(int32_t status) {
    switch (status) {
    case SEGMENT_FULL:
        return "complete";
    case SEGMENT_END:
        return "end of stream";
    case SEGMENT_SHORT:
        return "truncated";
    case SEGMENT_OVERFLOW:
        return "larger than the block size";
    default:
        return "corrupt";
    }
}

static void run_segment(zng_stream *strm, slot_t *slot, uint32_t block_size) {
    slot->status = inflate_segment(strm, slot, block_size);
    slot->in_used = (size_t)strm->total_in;
    slot->out_len = (size_t)strm->total_out;
    slot->crc = (uint32_t)zng_crc32_z(0, slot->out, slot->out_len);
}

/* Deflate one block on a fresh raw stream. A full flush ends it on a byte boundary with the empty
   stored block marker, the last block ends the deflate stream instead. */
static void run_block(zng_stream *strm, slot_t *slot, size_t out_size) {
    int32_t err;
    zng_deflateReset(strm);
    zng_deflateParams(strm, slot->level, slot->strategy);
    strm->next_in = (z_const uint8_t *)slot->in;
    strm->avail_in = (uint32_t)slot->in_len;
    strm->next_out = slot->out;
    strm->avail_out = (uint32_t)out_size;
    err = zng_deflate(strm, slot->last ? Z_FINISH : Z_SYNC_FLUSH);
    if (!slot->last && err == Z_OK)
        err = zng_deflate(strm, Z_FULL_FLUSH); /* the second marker makes it a boundary */
    slot->out_len = out_size - strm->avail_out;
    slot->in_used = slot->in_len - strm->avail_in;
    slot->status = slot->last ? (err == Z_STREAM_END ? 0 : -1)
                              : (err == Z_OK && strm->avail_in == 0 && strm->avail_out != 0 ? 0 : -1);
    slot->crc = (uint32_t)zng_crc32_z(0, slot->in, slot->in_len);
}

int32_t codec_init(pool_t *pool, zng_stream *strm) {
    memset(strm, 0, sizeof(*strm));
    if (pool->mode == POOL_DEFLATE)
        return zng_deflateInit2(strm, pool->level, Z_DEFLATED, -MAX_WBITS, 8, pool->strategy);
    return zng_inflateInit2(strm, -MAX_WBITS);
}

void codec_end(pool_t *pool, zng_stream *strm) {
    if (pool->mode == POOL_DEFLATE)
        zng_deflateEnd(strm);
    else
        zng_inflateEnd(strm);
}

void codec_run(pool_t *pool, zng_stream *strm, slot_t *slot) {
    if (pool->mode == POOL_DEFLATE)
        run_block(strm, slot, pool->out_size);
    else
        run_segment(strm, slot, pool->block_size);
}
