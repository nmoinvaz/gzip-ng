/* gzblockwrite.c -- the parallel writer for gzip members made of independent deflate blocks
 * For conditions of distribution and use, see LICENSE.md
 */

#include "gzblock_p.h"


struct gzblock_writer_s {
    gzblock_write_fn write;
    void *ctx;
    uint32_t block_size;
    int level, strategy, nthreads;
    pool_t pool;
    int pool_up;
    size_t next_produce, next_emit;
    slot_t *cur;            /* slot being filled */
    uint32_t crc;
    size_t total;
    int hdr_written, finished, failed;
    uint32_t meta_mtime;
    char meta_name[GZBLOCK_NAME_MAX];
    int rsyncable;          /* end blocks at rolling hash hits so edits stay local */
    uint32_t rhash, rmask;
    size_t rmin;            /* no early end before this much of the block is filled */
    int err;                /* zlib error code once failed */
    char msg[MSG_LEN];

    /* A block that has to be flushed part way continues on the calling thread as one deflate
       stream, so it still decodes to exactly block_size bytes. */
    zng_stream iz;
    int iz_init, inline_active;
    size_t inline_fill;     /* input bytes of the inline block so far */
    uint32_t inline_crc;
    uint8_t *obuf;          /* IO_CHUNK of output space for the inline stream */
};

/* ===========================================================================
 * Errors, output, and the member header
 * =========================================================================== */

static int writer_fail(gzblock_writer *w, int err, const char *msg) {
    snprintf(w->msg, sizeof(w->msg), "%s", msg);
    w->err = err;
    w->failed = 1;
    return -1;
}

static int writer_out(gzblock_writer *w, const uint8_t *buf, size_t len) {
    if (w->write(w->ctx, buf, len) != len)
        return writer_fail(w, Z_ERRNO, "write error");
    return 0;
}

/* gzip header with FEXTRA carrying the "ZB" subfield. Fixed 21-byte layout, the block size at
   offsets 16..19 and the flags byte at 20, which tests and tools rely on. */
static int writer_header(gzblock_writer *w) {
    uint8_t buf[FORMAT_HEADER_MAX];
    format_header hdr;
    size_t n;

    if (w->hdr_written)
        return 0;
    memset(&hdr, 0, sizeof(hdr));
    hdr.block_size = w->block_size;
    hdr.zb_flags = ZB_PAIRED;
    hdr.mtime = w->meta_mtime;
    hdr.name = w->meta_name[0] != 0 ? w->meta_name : NULL;
    hdr.level = w->level;
    hdr.strategy = w->strategy;
    n = format_header_build(buf, &hdr);
    w->hdr_written = 1;
    return writer_out(w, buf, n);
}

int gzblock_wrsyncable(gzblock_writer *w, int on) {
    uint32_t bits = 12;
    if (w == NULL || w->hdr_written || w->failed)
        return -1;
    w->rsyncable = on != 0;
    w->rmin = w->block_size / 2;
    while ((1u << bits) < (uint32_t)w->rmin && bits < 24)
        bits++;
    w->rmask = (1u << bits) - 1;
    return 0;
}

int gzblock_wmeta(gzblock_writer *w, uint32_t mtime, const char *name) {
    if (w == NULL || w->hdr_written || w->failed)
        return -1;
    w->meta_mtime = mtime;
    if (name != NULL && strlen(name) < GZBLOCK_NAME_MAX)
        memcpy(w->meta_name, name, strlen(name) + 1);
    return 0;
}

/* Write out the next compressed block in order. */
static int writer_drain(gzblock_writer *w) {
    slot_t *slot = pool_slot(&w->pool, w->next_emit);

    pool_wait(&w->pool, slot);
    if (slot->status != 0)
        return writer_fail(w, Z_STREAM_ERROR, "deflate failed");
    if (writer_header(w) != 0 || writer_out(w, slot->out, slot->out_len) != 0)
        return -1;
    w->crc = (uint32_t)zng_crc32_combine(w->crc, slot->crc, (z_off64_t)slot->in_len);
    w->total += slot->in_len;
    pool_release(&w->pool, slot);
    w->next_emit++;
    return 0;
}

/* ===========================================================================
 * Flush and parameter changes continue the block on the calling thread
 * =========================================================================== */

/* Run the inline stream with flush until its output is drained to the file. */
static int writer_inline_out(gzblock_writer *w, int flush) {
    int err;
    do {
        size_t have;
        w->iz.next_out = w->obuf;
        w->iz.avail_out = IO_CHUNK;
        err = zng_deflate(&w->iz, flush);
        if (err == Z_STREAM_ERROR)
            return writer_fail(w, Z_STREAM_ERROR, "deflate failed");
        have = IO_CHUNK - w->iz.avail_out;
        if (have != 0 && writer_out(w, w->obuf, have) != 0)
            return -1;
    } while (w->iz.avail_out == 0);
    return 0;
}

/* The inline block is complete, seal it the way the pool does and account for it. */
static int writer_inline_end(gzblock_writer *w, int last) {
    if (writer_inline_out(w, last ? Z_FINISH : Z_SYNC_FLUSH) != 0)
        return -1;
    if (!last && writer_inline_out(w, Z_FULL_FLUSH) != 0)
        return -1;
    w->crc = (uint32_t)zng_crc32_combine(w->crc, w->inline_crc, (z_off64_t)w->inline_fill);
    w->total += w->inline_fill;
    w->inline_active = 0;
    return 0;
}

/* Feed len bytes, at most what is left of the block, to the inline stream. */
static int writer_inline_feed(gzblock_writer *w, const uint8_t *buf, size_t len) {
    w->iz.next_in = (z_const uint8_t *)buf;
    w->iz.avail_in = (uint32_t)len;
    w->inline_crc = (uint32_t)zng_crc32_z(w->inline_crc, buf, len);
    w->inline_fill += len;
    if (writer_inline_out(w, Z_NO_FLUSH) != 0)
        return -1;
    if (w->inline_fill == w->block_size)
        return writer_inline_end(w, 0);
    return 0;
}

static int writer_drain(gzblock_writer *w);

/* Move the block being filled onto the inline stream. Everything before it goes to the file first,
   so the inline output can follow directly. */
static int writer_inline_begin(gzblock_writer *w) {
    while (w->next_emit < w->next_produce) {
        if (writer_drain(w) != 0)
            return -1;
    }
    if (writer_header(w) != 0)
        return -1;
    if (!w->iz_init) {
        memset(&w->iz, 0, sizeof(w->iz));
        if (zng_deflateInit2(&w->iz, w->level, Z_DEFLATED, -MAX_WBITS, 8, w->strategy) != Z_OK)
            return writer_fail(w, Z_MEM_ERROR, "out of memory");
        w->iz_init = 1;
    } else {
        zng_deflateReset(&w->iz);
        zng_deflateParams(&w->iz, w->level, w->strategy);
    }
    w->inline_active = 1;
    w->inline_fill = 0;
    w->inline_crc = 0;
    if (w->cur != NULL) {
        slot_t *slot = w->cur;
        w->cur = NULL;
        if (slot->in_len != 0 && writer_inline_feed(w, slot->in, slot->in_len) != 0)
            return -1;
        pool_release(&w->pool, slot);
    }
    return 0;
}

/* ===========================================================================
 * Blocks through the pool, filled in order, written out in order
 * =========================================================================== */

/* Take the next free slot to fill, draining finished ones to make room. */
static int writer_acquire(gzblock_writer *w) {
    slot_t *slot;
    while ((slot = pool_slot(&w->pool, w->next_produce))->state != SLOT_FREE) {
        if (writer_drain(w) != 0)
            return -1;
    }
    slot->in_len = 0;
    w->cur = slot;
    return 0;
}

static void writer_submit(gzblock_writer *w, int last) {
    w->cur->last = last;
    w->cur->level = w->level;
    w->cur->strategy = w->strategy;
    pool_submit(&w->pool, w->cur);
    w->cur = NULL;
    w->next_produce++;
}

/* ===========================================================================
 * The writer object
 * =========================================================================== */

gzblock_writer *gzblock_wopen(gzblock_write_fn write, void *ctx, int level, int strategy,
                                         uint32_t block_size, int nthreads) {
    gzblock_writer *w;
    zng_stream bound;
    size_t out_cap;

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

    /* Room for a whole block's worst case plus the flush marker. */
    memset(&bound, 0, sizeof(bound));
    if (zng_deflateInit2(&bound, level, Z_DEFLATED, -MAX_WBITS, 8, strategy) != Z_OK) {
        free(w);
        return NULL;
    }
    out_cap = zng_deflateBound(&bound, block_size) + 32;
    zng_deflateEnd(&bound);


    w->pool.codec.init = gzblock_codec_init;
    w->pool.codec.end = gzblock_codec_end;
    w->pool.codec.run = gzblock_codec_run;    w->pool.mode = POOL_DEFLATE;
    w->pool.block_size = block_size;
    w->pool.level = level;
    w->pool.strategy = strategy;
    w->obuf = (uint8_t *)malloc(IO_CHUNK);
    if (w->obuf == NULL || pool_alloc(&w->pool, w->nthreads, block_size, out_cap) != 0 ||
        pool_start(&w->pool, w->nthreads) != 0) {
        pool_free(&w->pool);
        free(w->obuf);
        free(w);
        return NULL;
    }
    w->pool_up = 1;
    return w;
}

int gzblock_write(gzblock_writer *w, const uint8_t *buf, size_t len) {
    if (w->failed || w->finished)
        return -1;
    while (len != 0) {
        size_t take;
        if (w->inline_active) {
            take = w->block_size - w->inline_fill;
            if (take > len)
                take = len;
            if (writer_inline_feed(w, buf, take) != 0)
                return -1;
            buf += take;
            len -= take;
            continue;
        }
        if (w->cur == NULL && writer_acquire(w) != 0)
            return -1;
        take = w->block_size - w->cur->in_len;
        if (take > len)
            take = len;
        if (w->rsyncable) {
            /* A hash hit after the minimum fill ends the block there, so boundaries follow the
               content and an edit re-aligns at the next hit instead of shifting every block. */
            size_t k;
            for (k = 0; k < take; k++) {
                w->rhash = ((w->rhash << 1) ^ buf[k]) & 0xffffffu;
                if (w->cur->in_len + k + 1 >= w->rmin && (w->rhash & w->rmask) == w->rmask) {
                    take = k + 1;
                    break;
                }
            }
        }
        memcpy(w->cur->in + w->cur->in_len, buf, take);
        w->cur->in_len += take;
        buf += take;
        len -= take;
        if (w->cur->in_len == w->block_size ||
            (w->rsyncable && w->cur->in_len >= w->rmin && (w->rhash & w->rmask) == w->rmask))
            writer_submit(w, 0);
    }
    return 0;
}

static int writer_inline_migrate(gzblock_writer *w);

int gzblock_wsetparams(gzblock_writer *w, int level, int strategy) {
    if (w->failed || w->finished)
        return -1;
    if (level == w->level && strategy == w->strategy)
        return 0;
    /* Input already taken for the current block keeps the old settings. deflateParams() applies
       them to it and switches mid-stream, so the block stays one stream. */
    if (writer_inline_migrate(w) != 0)
        return -1;
    if (w->inline_active) {
        int err;
        {
            for (;;) {
                size_t have;
                w->iz.next_out = w->obuf;
                w->iz.avail_out = IO_CHUNK;
                err = zng_deflateParams(&w->iz, level, strategy);
                have = IO_CHUNK - w->iz.avail_out;
                if (have != 0 && writer_out(w, w->obuf, have) != 0)
                    return -1;
                if (err != Z_BUF_ERROR)
                    break;
            }
            if (err != Z_OK)
                return writer_fail(w, Z_STREAM_ERROR, "deflateParams failed");
        }
    }
    w->level = level;
    w->strategy = strategy;
    return 0;
}

/* A partly filled block moves to the inline stream, so it can be flushed or reconfigured without
   ending early. No-op when there is no such block. */
static int writer_inline_migrate(gzblock_writer *w) {
    if (!w->inline_active && w->cur != NULL && w->cur->in_len != 0)
        return writer_inline_begin(w);
    return 0;
}

int gzblock_wflush(gzblock_writer *w) {
    if (w->failed || w->finished)
        return -1;
    if (writer_inline_migrate(w) != 0)
        return -1;
    if (w->inline_active)
        return writer_inline_out(w, Z_SYNC_FLUSH);
    while (w->next_emit < w->next_produce) {
        if (writer_drain(w) != 0)
            return -1;
    }
    return writer_header(w);
}

int gzblock_wfinish(gzblock_writer *w) {
    uint8_t trailer[GZ_TRAILER];

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
        while (w->next_emit < w->next_produce) {
            if (writer_drain(w) != 0)
                return -1;
        }
    }
    format_trailer_build(trailer, w->crc, (uint64_t)w->total);
    if (writer_out(w, trailer, sizeof(trailer)) != 0)
        return -1;
    w->finished = 1;
    return 0;
}

const char *gzblock_werror(const gzblock_writer *w) {
    return w->msg;
}

int gzblock_werrcode(const gzblock_writer *w) {
    return w->err;
}

void gzblock_wclose(gzblock_writer *w) {
    if (w == NULL)
        return;
    if (w->pool_up)
        pool_stop(&w->pool);
    pool_free(&w->pool);
    if (w->iz_init)
        zng_deflateEnd(&w->iz);
    free(w->obuf);
    free(w);
}

