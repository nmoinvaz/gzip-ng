/* writer.c -- the parallel writer for gzip members made of independent deflate blocks
 * For conditions of distribution and use, see LICENSE.md
 */

#include "gzblock_p.h"

struct gzblock_writer_s {
    gzblock_write_fn write;
    void *ctx;
    uint32_t block_size;
    int32_t level, strategy, nthreads;
    pipeline_t pipeline;
    slot_t *cur; /* slot being filled */
    uint32_t crc;
    size_t total_in;
    int32_t hdr_written, finished, failed;
    uint32_t mtime;
    char name[FORMAT_NAME_MAX];
    int32_t rsyncable; /* end blocks at rolling hash hits so edits stay local */
    uint32_t rsync_hash, rsync_mask;
    size_t rsync_max; /* a block is cut on size alone only here */
    size_t rsync_min; /* no early end before this much of the block is filled */
    int32_t err;      /* zlib error code once failed */
    char msg[MSG_LEN];

    /* A block that has to be flushed part way continues on the calling thread as one deflate
       stream, so a flush does not shorten it. Blocks end at block_size, or at a rolling hash hit
       when the writer is rsyncable. */
    zng_stream strm;
    int32_t strm_init, inline_active;
    size_t inline_fill; /* input bytes of the inline block so far */
    uint32_t inline_crc;
    uint8_t *obuf; /* IO_CHUNK of output space for the inline stream */
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
 * Inline continuation for flush and parameter changes
 */

/* Run the inline stream with flush until its output is drained to the file. */
static int32_t writer_inline_out(gzblock_writer *w, int32_t flush) {
    int32_t err;
    do {
        size_t have;
        w->strm.next_out = w->obuf;
        w->strm.avail_out = IO_CHUNK;
        err = zng_deflate(&w->strm, flush);
        if (err == Z_STREAM_ERROR)
            return writer_fail(w, Z_STREAM_ERROR, "deflate failed");
        have = IO_CHUNK - w->strm.avail_out;
        if (have != 0 && writer_out(w, w->obuf, have) != 0)
            return -1;
    } while (w->strm.avail_out == 0);
    return 0;
}

/* The inline block is complete, seal it the way the pool does and account for it. */
static int32_t writer_inline_end(gzblock_writer *w, int32_t last) {
    if (writer_inline_out(w, last ? Z_FINISH : Z_SYNC_FLUSH) != 0)
        return -1;
    if (!last && writer_inline_out(w, Z_FULL_FLUSH) != 0)
        return -1;
    w->crc = (uint32_t)zng_crc32_combine(w->crc, w->inline_crc, (z_off64_t)w->inline_fill);
    w->total_in += w->inline_fill;
    w->inline_active = 0;
    return 0;
}

/* Feed len bytes, at most what is left of the block, to the inline stream. */
static int32_t writer_inline_feed(gzblock_writer *w, const uint8_t *buf, size_t len) {
    w->strm.next_in = (z_const uint8_t *)buf;
    w->strm.avail_in = (uint32_t)len;
    w->inline_crc = (uint32_t)zng_crc32_z(w->inline_crc, buf, len);
    w->inline_fill += len;
    if (writer_inline_out(w, Z_NO_FLUSH) != 0)
        return -1;
    if (w->inline_fill == w->block_size)
        return writer_inline_end(w, 0);
    return 0;
}

static int32_t writer_drain(gzblock_writer *w);

static int32_t writer_drain_all(gzblock_writer *w) {
    while (pipeline_has_pending(&w->pipeline)) {
        if (writer_drain(w) != 0)
            return -1;
    }
    return 0;
}

/* Move the block being filled onto the inline stream. Everything before it goes to the file first,
   so the inline output can follow directly. */
static int32_t writer_inline_begin(gzblock_writer *w) {
    if (writer_drain_all(w) != 0)
        return -1;
    if (writer_header(w) != 0)
        return -1;
    if (!w->strm_init) {
        memset(&w->strm, 0, sizeof(w->strm));
        if (zng_deflateInit2(&w->strm, w->level, Z_DEFLATED, -MAX_WBITS, 8, w->strategy) != Z_OK)
            return writer_fail(w, Z_MEM_ERROR, "out of memory");
        w->strm_init = 1;
    } else {
        zng_deflateReset(&w->strm);
        zng_deflateParams(&w->strm, w->level, w->strategy);
    }
    w->inline_active = 1;
    w->inline_fill = 0;
    w->inline_crc = 0;
    if (w->cur != NULL) {
        slot_t *slot = w->cur;
        w->cur = NULL;
        if (slot->in_len != 0 && writer_inline_feed(w, slot->in, slot->in_len) != 0)
            return -1;
        pool_release(&w->pipeline.pool, slot);
    }
    return 0;
}

/* A partly filled block moves to the inline stream, so it can be flushed or reconfigured without
   ending early. No-op when there is no such block. */
static int32_t writer_inline_migrate(gzblock_writer *w) {
    if (!w->inline_active && w->cur != NULL && w->cur->in_len != 0)
        return writer_inline_begin(w);
    return 0;
}

/* ===========================================================================
 * Blocks through the pool
 */

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
    if (slot->status != 0)
        return writer_fail(w, Z_STREAM_ERROR, "deflate failed");
    if (writer_header(w) != 0 || writer_out(w, slot->out, slot->out_len) != 0)
        return -1;
    w->crc = (uint32_t)zng_crc32_combine(w->crc, slot->crc, (z_off64_t)slot->in_len);
    w->total_in += slot->in_len;
    pool_release(&w->pipeline.pool, slot);
    w->pipeline.next_drain++;
    return 0;
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

    if (w->pipeline.started)
        pipeline_free(&w->pipeline);
    if (pipeline_start(&w->pipeline, w->nthreads, in_size, out_size) != 0)
        return -1;
    return 0;
}

gzblock_writer *gzblock_writer_open(gzblock_write_fn write, void *ctx, int32_t level, int32_t strategy,
                                    uint32_t block_size, int32_t nthreads) {
    gzblock_writer *w;

    if (write == NULL || block_size == 0 || block_size > GZBLOCK_MAX_BLOCK)
        return NULL;
    w = (gzblock_writer *)calloc(1, sizeof(*w));
    if (w == NULL)
        return NULL;
    w->write = write;
    w->ctx = ctx;
    w->block_size = block_size;
    w->level = level;
    w->strategy = strategy;
    w->nthreads = nthreads > 0 ? nthreads : pool_default_threads();

    w->pipeline.pool.mode = POOL_DEFLATE;
    w->pipeline.pool.block_size = block_size;
    w->pipeline.pool.level = level;
    w->pipeline.pool.strategy = strategy;
    w->obuf = (uint8_t *)malloc(IO_CHUNK);
    if (w->obuf == NULL || writer_pool_size(w, block_size) != 0) {
        pipeline_free(&w->pipeline);
        free(w->obuf);
        free(w);
        return NULL;
    }
    return w;
}

/* With --rsyncable the block size is a target rather than a ceiling. A boundary is wanted
   every block_size / 2 bytes and refused before that much is buffered, which averages one block
   of block_size, and a block is cut on size alone only at twice it. That headroom leaves all but
   a few percent of boundaries content-defined. */
int32_t gzblock_writer_rsyncable(gzblock_writer *w, int32_t on) {
    if (w == NULL || w->hdr_written || w->failed)
        return -1;
    if (!on) {
        w->rsyncable = 0;
        return 0;
    }
    w->rsync_min = w->block_size / 2;
    w->rsync_mask = rolling_mask(w->rsync_min);
    w->rsync_max = (size_t)w->block_size * 2;
    if (writer_pool_size(w, w->rsync_max) != 0)
        return w->failed = 1, -1;
    w->rsyncable = 1;
    return 0;
}

int32_t gzblock_writer_meta(gzblock_writer *w, uint32_t mtime, const char *name) {
    if (w == NULL || w->hdr_written || w->failed)
        return -1;
    w->mtime = mtime;
    if (name != NULL && strlen(name) < FORMAT_NAME_MAX)
        memcpy(w->name, name, strlen(name) + 1);
    return 0;
}

/* A hash hit after the minimum fill ends the block there, so boundaries follow the content and
   an edit re-aligns at the next hit instead of shifting every block. Shortens take to end at the
   first such hit. Returns 1 on a hit. */
static int32_t writer_rsync_cut(gzblock_writer *w, const uint8_t *buf, size_t *take) {
    size_t fill = w->cur->in_len;
    size_t first = fill + 1 >= w->rsync_min ? 0 : w->rsync_min - fill - 1;
    size_t hit = rolling_find(&w->rsync_hash, w->rsync_mask, buf, *take, first);

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
        if (w->inline_active) {
            take = MIN(w->block_size - w->inline_fill, len);
            if (writer_inline_feed(w, buf, take) != 0)
                return -1;
            buf += take;
            len -= take;
            continue;
        }
        if (w->cur == NULL && writer_acquire(w) != 0)
            return -1;
        limit = w->rsyncable ? w->rsync_max : w->block_size;
        take = MIN(limit - w->cur->in_len, len);
        hit = w->rsyncable && writer_rsync_cut(w, buf, &take);
        memcpy(w->cur->in + w->cur->in_len, buf, take);
        w->cur->in_len += take;
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
    /* Input already taken for the current block keeps the old settings. deflateParams() applies
       them to it and switches mid-stream, so the block stays one stream. */
    if (writer_inline_migrate(w) != 0)
        return -1;
    if (w->inline_active) {
        int32_t err;
        for (;;) {
            size_t have;
            w->strm.next_out = w->obuf;
            w->strm.avail_out = IO_CHUNK;
            err = zng_deflateParams(&w->strm, level, strategy);
            have = IO_CHUNK - w->strm.avail_out;
            if (have != 0 && writer_out(w, w->obuf, have) != 0)
                return -1;
            if (err != Z_BUF_ERROR)
                break;
        }
        if (err != Z_OK)
            return writer_fail(w, Z_STREAM_ERROR, "deflateParams failed");
    }
    w->level = level;
    w->strategy = strategy;
    return 0;
}

int32_t gzblock_writer_flush(gzblock_writer *w) {
    if (w->failed || w->finished)
        return -1;
    if (writer_inline_migrate(w) != 0)
        return -1;
    if (w->inline_active)
        return writer_inline_out(w, Z_SYNC_FLUSH);
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
    if (w->inline_active) {
        /* The inline block is the last one and ends the stream itself. */
        if (writer_inline_end(w, 1) != 0)
            return -1;
    } else {
        /* The last block ends the deflate stream, an empty one if the input ended on a boundary. */
        if (w->cur == NULL && writer_acquire(w) != 0)
            return -1;
        writer_submit(w, 1);
        if (writer_drain_all(w) != 0)
            return -1;
    }
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
    if (w == NULL)
        return;
    pipeline_free(&w->pipeline);
    if (w->strm_init)
        zng_deflateEnd(&w->strm);
    free(w->obuf);
    free(w);
}
