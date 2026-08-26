/* codec.c -- the deflate and inflate the pool runs over one slot at a time
 * For conditions of distribution and use, see LICENSE.md
 */

#include "gzblock_p.h"

static void run_segment(zng_stream *z, slot_t *slot, uint32_t block_size) {
    block_dec d;
    size_t offset = 0, used;
    int status;

    /* Strict blocks must fill exactly block_size, so a reused larger buffer is capped for them. */
    blockdec_begin(&d, z, slot->out, slot->pair || slot->last ? (uint32_t)slot->out_cap : block_size);
    d.accept_partial = slot->pair;
    for (;;) {
        status = blockdec_feed(&d, slot->in + offset, slot->in_len - offset, &used);
        offset += used;
        /* Pair-terminated and final segments may hold several coalesced chunks, so their output
           grows on demand. Their validity never rested on the size, growing stays safe. */
        if (status == SEG_OVERFLOW && (slot->pair || slot->last) && slot->out_cap < GZBLOCK_MAX_BLOCK) {
            size_t ncap = slot->out_cap * 2;
            uint8_t *grown;
            if (ncap > GZBLOCK_MAX_BLOCK)
                ncap = GZBLOCK_MAX_BLOCK;
            grown = (uint8_t *)realloc(slot->out, ncap);
            if (grown == NULL) {
                status = SEG_ERROR;
                break;
            }
            z->next_out = grown + (size_t)z->total_out;
            z->avail_out += (uint32_t)(ncap - slot->out_cap);
            slot->out = grown;
            slot->out_cap = ncap;
            d.want_marker = 0; /* output is no longer full, back to normal decoding */
            continue;
        }
        break;
    }
    slot->status = status;
    slot->in_used = offset;
    slot->out_len = (size_t)z->total_out;
    slot->crc = (uint32_t)zng_crc32_z(0, slot->out, slot->out_len);
}

/* Deflate one block on a fresh raw stream. A full flush ends it on a byte boundary with the empty
   stored block marker, the last block ends the deflate stream instead. */
static void run_block(zng_stream *z, slot_t *slot, size_t out_cap) {
    int err;
    zng_deflateReset(z);
    zng_deflateParams(z, slot->level, slot->strategy);
    z->next_in = (z_const uint8_t *)slot->in;
    z->avail_in = (uint32_t)slot->in_len;
    z->next_out = slot->out;
    z->avail_out = (uint32_t)out_cap;
    err = zng_deflate(z, slot->last ? Z_FINISH : Z_SYNC_FLUSH);
    if (!slot->last && err == Z_OK)
        err = zng_deflate(z, Z_FULL_FLUSH); /* the second marker makes it a boundary */
    slot->out_len = out_cap - z->avail_out;
    slot->in_used = slot->in_len - z->avail_in;
    slot->status =
        slot->last ? (err == Z_STREAM_END ? 0 : -1) : (err == Z_OK && z->avail_in == 0 && z->avail_out != 0 ? 0 : -1);
    slot->crc = (uint32_t)zng_crc32_z(0, slot->in, slot->in_len);
}

int gzblock_codec_init(pool_t *pool, zng_stream *z) {
    memset(z, 0, sizeof(*z));
    if (pool->mode == POOL_DEFLATE)
        return zng_deflateInit2(z, pool->level, Z_DEFLATED, -MAX_WBITS, 8, pool->strategy);
    return zng_inflateInit2(z, -MAX_WBITS);
}

void gzblock_codec_end(pool_t *pool, zng_stream *z) {
    if (pool->mode == POOL_DEFLATE)
        zng_deflateEnd(z);
    else
        zng_inflateEnd(z);
}

void gzblock_codec_run(pool_t *pool, zng_stream *z, slot_t *slot) {
    if (pool->mode == POOL_DEFLATE)
        run_block(z, slot, pool->out_cap);
    else
        run_segment(z, slot, pool->block_size);
}
