/* writer.c -- the parallel writer for gzip members made of independent deflate blocks
 * For conditions of distribution and use, see LICENSE.md
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec.h"
#include "format.h"
#include "gzblock.h"
#include "pipeline.h"
#include "pool.h"
#include "rolling.h"
#include "util.h"
#include "zlib-ng.h"

struct gzblock_writer_s {
    gzblock_write_fn write;
    void *ctx;
    uint32_t block_size;
    int32_t level;
    int32_t strategy;
    int32_t nthreads;
    pipeline_t pipeline;
    slot_t *cur; /* slot being filled */
    uint32_t crc;
    size_t total_in;
    int32_t hdr_written;
    int32_t finished;
    int32_t failed;
    uint32_t mtime;
    char name[FORMAT_NAME_MAX];
    int32_t rsyncable; /* end blocks at rolling hash hits so edits stay local */
    uint32_t rsync_hash;
    uint32_t rsync_mask_lo; /* strict mask while the block is short of block_size */
    uint32_t rsync_mask_hi; /* loose mask once it is past */
    size_t rsync_max; /* a block is cut on size alone only here */
    size_t rsync_min; /* no early end before this much of the block is filled */
    int32_t err;      /* zlib error code once failed */
    char msg[MSG_LEN];
};

/* ===========================================================================
 * Errors, output, and the member header
 */

static int32_t writer_fail(gzblock_writer *w, int32_t err, const char *msg) {
    snprintf(w->msg, sizeof(w->msg), "%s", msg);
    w->err = err;
    w->failed = 1;
    return -1;
}

static int32_t writer_out(gzblock_writer *w, const uint8_t *buf, size_t len) {
    if (w->write(w->ctx, buf, len) != len)
        return writer_fail(w, Z_ERRNO, "write error");
    return 0;
}

/* An ordinary gzip header, carrying only the name and time --name and --time ask for. Nothing in it
   marks the member as cut into blocks, a reader finds that out by scanning. */
static int32_t writer_header(gzblock_writer *w) {
    uint8_t buf[FORMAT_HEADER_MAX];
    format_header hdr;
    size_t n;

    if (w->hdr_written)
        return 0;
    memset(&hdr, 0, sizeof(hdr));
    hdr.mtime = w->mtime;
    hdr.name = w->name[0] != 0 ? w->name : NULL;
    hdr.level = w->level;
    hdr.strategy = w->strategy;
    n = format_header_build(buf, &hdr);
    w->hdr_written = 1;
    return writer_out(w, buf, n);
}

/* ===========================================================================
 * Blocks through the pool
 */

static int32_t writer_drain(gzblock_writer *w);

/* Take the next free slot to fill, draining finished ones to make room. */
static int32_t writer_acquire(gzblock_writer *w) {
    slot_t *slot;
    while ((slot = pool_slot(&w->pipeline.pool, w->pipeline.next_submit))->state != SLOT_FREE) {
        if (writer_drain(w) != 0)
            return -1;
    }
    slot->in_len = 0;
    w->cur = slot;
    return 0;
}

static void writer_submit(gzblock_writer *w, int32_t last) {
    w->cur->last = last;
    w->cur->level = w->level;
    w->cur->strategy = w->strategy;
    pipeline_submit(&w->pipeline, w->cur);
    w->cur = NULL;
}

/* Write out the next compressed block in order. */
static int32_t writer_drain(gzblock_writer *w) {
    slot_t *slot = pipeline_wait(&w->pipeline, w->pipeline.next_drain);
    if (slot->status != BLOCK_OK)
        return writer_fail(w, Z_STREAM_ERROR, "deflate failed");
    if (writer_header(w) != 0 || writer_out(w, slot->out, slot->out_len) != 0)
        return -1;
    w->crc = (uint32_t)zng_crc32_combine(w->crc, slot->crc, (z_off64_t)slot->in_len);
    w->total_in += slot->in_len;
    pool_release(&w->pipeline.pool, slot);
    pipeline_drained(&w->pipeline);
    return 0;
}

static int32_t writer_drain_all(gzblock_writer *w) {
    while (pipeline_has_pending(&w->pipeline)) {
        if (writer_drain(w) != 0)
            return -1;
    }
    return 0;
}

/* End the block being filled early. A marker pair ends every block, which makes any length a
   valid one. */
static void writer_cut(gzblock_writer *w) {
    if (w->cur && w->cur->in_len != 0)
        writer_submit(w, 0);
}

/* ===========================================================================
 * The writer object
 */

/* Size the ring for blocks of up to in_size input bytes and start the workers. Called again when
   --rsyncable widens the limit, which happens before any block is submitted. */
static int32_t writer_pool_size(gzblock_writer *w, size_t in_size) {
    zng_stream bound;
    size_t out_size;

    memset(&bound, 0, sizeof(bound));
    if (zng_deflateInit2(&bound, w->level, Z_DEFLATED, -MAX_WBITS, 8, w->strategy) != Z_OK)
        return -1;
    out_size = zng_deflateBound(&bound, in_size) + 32;
    zng_deflateEnd(&bound);

    pipeline_free(&w->pipeline);
    if (pipeline_start(&w->pipeline, w->nthreads, in_size, out_size) != 0)
        return -1;
    return 0;
}

gzblock_writer *gzblock_writer_open(gzblock_write_fn write, void *ctx, int32_t level, int32_t strategy,
                                    uint32_t block_size, int32_t nthreads) {
    gzblock_writer *w;

    if (!write || block_size == 0 || block_size > GZBLOCK_MAX_BLOCK)
        return NULL;
    w = (gzblock_writer *)calloc(1, sizeof(*w));
    if (!w)
        return NULL;
    w->write = write;
    w->ctx = ctx;
    w->block_size = block_size;
    w->level = level;
    w->strategy = strategy;
    w->nthreads = nthreads > 0 ? nthreads : pool_default_threads();

    w->pipeline.pool.mode = POOL_DEFLATE;
    w->pipeline.pool.level = level;
    w->pipeline.pool.strategy = strategy;
    if (writer_pool_size(w, block_size) != 0) {
        pipeline_free(&w->pipeline);
        free(w);
        return NULL;
    }
    return w;
}

/* With --rsyncable the block size is a target rather than a ceiling. Boundaries are refused
   before half a block is buffered and forced at twice one, and between those FastCDC's
   normalized cut crowds them just past block_size, a strict mask that rarely hits while the
   block is short of the target and a loose one that hits quickly once it is past. That puts the
   average about a tenth above block_size with every boundary content-defined. Content the hash
   never hits, such as a run of one byte, is still cut at twice it. */
int32_t gzblock_writer_rsyncable(gzblock_writer *w, int32_t on) {
    if (!w || w->hdr_written || w->failed)
        return -1;
    if (!on) {
        w->rsyncable = 0;
        return 0;
    }
    w->rsync_min = w->block_size / 2;
    w->rsync_mask_lo = rolling_mask((size_t)w->block_size * 4);
    w->rsync_mask_hi = rolling_mask(w->block_size / 8);
    w->rsync_max = (size_t)w->block_size * 2;
    if (writer_pool_size(w, w->rsync_max) != 0)
        return w->failed = 1, -1;
    w->rsyncable = 1;
    return 0;
}

int32_t gzblock_writer_meta(gzblock_writer *w, uint32_t mtime, const char *name) {
    if (!w || w->hdr_written || w->failed)
        return -1;
    w->mtime = mtime;
    if (name && strlen(name) < FORMAT_NAME_MAX)
        memcpy(w->name, name, strlen(name) + 1);
    return 0;
}

/* A hash hit after the minimum fill ends the block there, so boundaries follow the content and
   an edit re-aligns at the next hit instead of shifting every block. The strict mask covers the
   input up to a full block_size and the loose mask what lies past it. Shortens take to end at
   the first hit. Returns 1 on a hit. */
static int32_t writer_rsync_cut(gzblock_writer *w, const uint8_t *buf, size_t *take) {
    size_t fill = w->cur->in_len;
    size_t first = fill + 1 >= w->rsync_min ? 0 : w->rsync_min - fill - 1;
    size_t lo_end = fill >= w->block_size ? 0 : MIN(*take, w->block_size - fill);
    size_t hit = rolling_find(&w->rsync_hash, w->rsync_mask_lo, buf, lo_end, first);

    if (hit == lo_end && *take > lo_end)
        hit = rolling_find(&w->rsync_hash, w->rsync_mask_hi, buf, *take, lo_end);
    if (hit == *take)
        return 0;
    *take = hit + 1;
    return 1;
}

int32_t gzblock_writer_write(gzblock_writer *w, const uint8_t *buf, size_t len) {
    size_t limit = w->block_size;

    if (w->failed || w->finished)
        return -1;
    while (len != 0) {
        size_t take;
        int32_t hit;
        if (!w->cur && writer_acquire(w) != 0)
            return -1;
        limit = w->rsyncable ? w->rsync_max : w->block_size;
        take = MIN(limit - w->cur->in_len, len);
        hit = w->rsyncable && writer_rsync_cut(w, buf, &take);
        slot_append(w->cur, buf, take);
        buf += take;
        len -= take;
        if (w->cur->in_len == limit || hit)
            writer_submit(w, 0);
    }
    return 0;
}

int32_t gzblock_writer_setparams(gzblock_writer *w, int32_t level, int32_t strategy) {
    if (w->failed || w->finished)
        return -1;
    if (level == w->level && strategy == w->strategy)
        return 0;
    /* Input already taken for the current block keeps the old settings, in a block that ends
       here. */
    writer_cut(w);
    w->level = level;
    w->strategy = strategy;
    return 0;
}

int32_t gzblock_writer_flush(gzblock_writer *w) {
    if (w->failed || w->finished)
        return -1;
    writer_cut(w);
    if (writer_drain_all(w) != 0)
        return -1;
    return writer_header(w);
}

int32_t gzblock_writer_finish(gzblock_writer *w) {
    uint8_t trailer[FORMAT_TRAILER_LEN];

    if (w->failed)
        return -1;
    if (w->finished)
        return 0;
    /* The last block ends the deflate stream, an empty one if the input ended on a boundary. */
    if (!w->cur && writer_acquire(w) != 0)
        return -1;
    writer_submit(w, 1);
    if (writer_drain_all(w) != 0)
        return -1;
    format_trailer_build(trailer, w->crc, (uint64_t)w->total_in);
    if (writer_out(w, trailer, sizeof(trailer)) != 0)
        return -1;
    w->finished = 1;
    return 0;
}

const char *gzblock_writer_error(const gzblock_writer *w) {
    return w->msg;
}

int32_t gzblock_writer_errcode(const gzblock_writer *w) {
    return w->err;
}

void gzblock_writer_close(gzblock_writer *w) {
    if (!w)
        return;
    pipeline_free(&w->pipeline);
    free(w);
}
