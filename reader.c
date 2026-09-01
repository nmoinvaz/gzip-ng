/* reader.c -- the parallel reader for gzip members made of independent deflate blocks
 * For conditions of distribution and use, see LICENSE.md
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "codec.h"
#include "cutter.h"
#include "format.h"
#include "gzblock.h"
#include "pipeline.h"
#include "pool.h"
#include "scanner.h"
#include "util.h"
#include "zlib-ng.h"

enum {
    READER_HEADER,
    READER_PASSTHRU,
    READER_STREAM,
    READER_BLOCKS,
    READER_MEMBERS,
    READER_MEMBER_END,
    READER_END,
    READER_ERROR
};

/* Sized members, BGZF, cut whole and decoded in parallel with headers and trailers included. */
typedef struct {
    buf_t seg;     /* member group being assembled for a slot */
    int32_t count; /* whole members in seg */
    int32_t done;  /* the input's next bytes are not a sized member */
} reader_members;

struct gzblock_reader_s {
    gzblock_read_fn read;
    void *ctx;
    buf_t in_buf;   /* input in hand, not yet consumed */
    int32_t in_eof; /* the read callback returned 0 */

    zng_stream strm;  /* plain inflate */
    uint8_t *out_buf; /* IO_CHUNK, output of strm, or bytes passed through */

    int32_t state;
    int32_t nthreads;
    uint32_t block_hint; /* block size to assume when a header records none */
    int32_t members;     /* gzip members finished so far */
    int32_t err;         /* zlib error code once failed */
    char msg[MSG_LEN];

    const uint8_t *next; /* next output to deliver */
    size_t have;         /* bytes available at next */
    slot_t *slot;        /* slot to release once next is consumed, or NULL */

    reader_members memb;
    cutter_t cut;
    buf_t hdr;       /* this member's header, kept for the fallback */
    int32_t cut_all; /* the cutter handed out the member's last segment */

    pipeline_t pipeline;
    uint32_t crc;    /* running crc and length of the member's output */
    uint32_t crc_op; /* crc combine operator precomputed for the pool's block size */
    size_t total_out;
};

/* ===========================================================================
 * Errors, input, and output handoff
 */

static int32_t reader_fail(gzblock_reader *r, int32_t err, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->msg, sizeof(r->msg), fmt, ap);
    va_end(ap);
    r->err = err;
    r->state = READER_ERROR;
    return -1;
}

static int32_t reader_fill(gzblock_reader *r, size_t want) {
    if (buf_fill(&r->in_buf, r->read, r->ctx, want, &r->in_eof) != 0)
        return reader_fail(r, Z_ERRNO, "read error");
    return 0;
}

static int32_t reader_oom(gzblock_reader *r) {
    return reader_fail(r, Z_MEM_ERROR, "out of memory");
}

static void reader_handout(gzblock_reader *r, const uint8_t *p, size_t n, slot_t *slot) {
    r->next = p;
    r->have = n;
    r->slot = slot;
}

/* ===========================================================================
 * Plain inflate and pass-through
 */

/* Plain inflate of a member, starting with whatever is in buf. */
static void reader_start_stream(gzblock_reader *r) {
    zng_inflateReset(&r->strm);
    r->state = READER_STREAM;
}

static int32_t reader_stream(gzblock_reader *r) {
    size_t feed;
    int32_t err;

    if (r->in_buf.len == 0) {
        if (reader_fill(r, 1) != 0)
            return -1;
        if (r->in_buf.len == 0)
            return reader_fail(r, Z_BUF_ERROR, "unexpected end of file");
    }
    feed = clamp_u32(r->in_buf.len);
    r->strm.next_in = (z_const uint8_t *)buf_data(&r->in_buf);
    r->strm.avail_in = (uint32_t)feed;
    r->strm.next_out = r->out_buf;
    r->strm.avail_out = IO_CHUNK;
    err = zng_inflate(&r->strm, Z_NO_FLUSH);
    buf_drop(&r->in_buf, feed - r->strm.avail_in);
    if (err != Z_OK && err != Z_STREAM_END)
        return reader_fail(r, Z_DATA_ERROR, "inflate failed: %s", r->strm.msg ? r->strm.msg : "data error");
    reader_handout(r, r->out_buf, IO_CHUNK - r->strm.avail_out, NULL);
    if (err == Z_STREAM_END) {
        r->members++;
        r->state = READER_HEADER;
    }
    return 0;
}

/* Not gzip at all, copied through unchanged. */
static int32_t reader_passthru(gzblock_reader *r) {
    size_t n;
    if (r->in_buf.len != 0) {
        n = MIN(r->in_buf.len, IO_CHUNK);
        memcpy(r->out_buf, buf_data(&r->in_buf), n);
        buf_drop(&r->in_buf, n);
        reader_handout(r, r->out_buf, n, NULL);
        return 0;
    }
    if (r->in_eof) {
        r->state = READER_END;
        return 0;
    }
    n = r->read(r->ctx, r->out_buf, IO_CHUNK);
    if (n == (size_t)-1)
        return reader_fail(r, Z_ERRNO, "read error");
    if (n == 0) {
        r->in_eof = 1;
        r->state = READER_END;
        return 0;
    }
    reader_handout(r, r->out_buf, n, NULL);
    return 0;
}

/* ===========================================================================
 * The block pipeline
 */

/* Cut the next segment, feeding the cutter until it can decide. */
static int32_t reader_next_cut(gzblock_reader *r) {
    int32_t rc;
    while ((rc = cutter_next(&r->cut, &r->in_buf, r->in_eof)) == CUT_MORE) {
        if (reader_fill(r, r->in_buf.len + IO_CHUNK) != 0)
            return CUT_ERROR;
    }
    if (rc == CUT_ERROR)
        return reader_oom(r); /* the cutter's only failure is allocation, and reader_oom is -1 */
    return rc;
}

/* Start the pool for inflate, once, its ring shared by every later member. */
static int32_t reader_pipeline_start(gzblock_reader *r, uint32_t block_size) {
    int32_t rc;

    if (r->pipeline.started)
        return 0;
    r->pipeline.pool.mode = POOL_INFLATE;
    r->pipeline.pool.block_size = block_size;
    /* Segments are swapped in whole, so slots start without an in buffer. */
    rc = pipeline_start(&r->pipeline, r->nthreads, 0, block_size);
    if (rc == -1)
        return reader_oom(r);
    if (rc == -2)
        return reader_fail(r, Z_MEM_ERROR, "cannot start threads");
    return 0;
}

/* Enter block mode for a member whose header (the first hdr_len bytes of buf) records, or
   --blocksize supplies, a block size. */
static int32_t reader_start_blocks(gzblock_reader *r, size_t hdr_len, uint32_t block_size, int32_t paired) {
    r->hdr.len = 0;
    if (buf_append(&r->hdr, buf_data(&r->in_buf), hdr_len) != 0)
        return reader_oom(r);
    buf_drop(&r->in_buf, hdr_len);
    if (reader_pipeline_start(r, block_size) != 0)
        return -1;
    cutter_init(&r->cut, block_size, paired);
    r->cut_all = 0;
    pipeline_reset(&r->pipeline);
    r->crc_op = (uint32_t)zng_crc32_combine_gen((z_off64_t)r->pipeline.pool.block_size);
    r->crc = 0;
    r->total_out = 0;
    r->state = READER_BLOCKS;
    return 0;
}

/* Put prefix, every segment still in the ring, and the input in hand back at the front of the
   input, to be cut again. */
static int32_t reader_rewind(gzblock_reader *r, const uint8_t *prefix, size_t n) {
    buf_t all = {NULL, 0, 0, 0};
    size_t i;

    if (n != 0 && buf_append(&all, prefix, n) != 0)
        return reader_oom(r);
    /* Take back what no worker has started, newest first, so only claimed slots are waited on
       and their wasted inflation is bounded by the worker count. */
    for (i = r->pipeline.next_submit; i > r->pipeline.next_drain; i--) {
        if (!pool_cancel(&r->pipeline.pool, pool_slot(&r->pipeline.pool, i - 1)))
            break;
    }
    for (i = r->pipeline.next_drain; i < r->pipeline.next_submit; i++) {
        slot_t *slot = pipeline_wait(&r->pipeline, i);
        if (buf_append(&all, slot->in, slot->in_len) != 0)
            return reader_oom(r);
        pool_release(&r->pipeline.pool, slot);
    }
    if (buf_append(&all, buf_data(&r->in_buf), r->in_buf.len) != 0)
        return reader_oom(r);
    free(r->in_buf.p);
    r->in_buf = all;
    cutter_rescan(&r->cut, 0);
    r->cut_all = 0;
    pipeline_clear(&r->pipeline);
    return 0;
}

/* Put the member back together and stream it through plain inflate instead. Only valid before any
   of its output was handed out. */
static int32_t reader_fallback(gzblock_reader *r) {
    if (reader_rewind(r, r->hdr.p, r->hdr.len) != 0)
        return -1;
    reader_start_stream(r);
    return 0;
}

/* Keep the pool fed, cutting segments into free slots until the ring is full or the input is done. */
static int32_t reader_produce(gzblock_reader *r) {
    while (!r->cut_all) {
        slot_t *slot = pool_slot(&r->pipeline.pool, r->pipeline.next_submit);
        int32_t rc;

        if (slot->state != SLOT_FREE)
            break;
        rc = reader_next_cut(r);
        if (rc == CUT_DONE) {
            r->cut_all = 1;
            break;
        }
        if (rc == CUT_ERROR)
            return -1;
        if (rc == CUT_TOO_LARGE) {
            if (r->pipeline.next_submit == 0 && r->pipeline.next_drain == 0)
                return reader_fallback(r); /* no block structure at this size */
            return reader_fail(r, Z_DATA_ERROR, "block %zu is larger than the block size", r->pipeline.next_submit);
        }
        slot_swap_in(slot, &r->cut.seg);
        slot->last = r->cut.seg_last;
        slot->pair = r->cut.seg_pair;
        slot->members = 0;
        pipeline_submit(&r->pipeline, slot);
    }
    return 0;
}

/* Hand out a finished block and account for it. The slot goes back once its output is consumed. */
static void reader_block_out(gzblock_reader *r, slot_t *slot) {
    /* Nearly every block inflates to exactly the pool's block size, where the precomputed
       operator folds its crc in without the general combine's matrix walk. */
    if (slot->out_len == (size_t)r->pipeline.pool.block_size)
        r->crc = (uint32_t)zng_crc32_combine_op(r->crc, slot->crc, r->crc_op);
    else
        r->crc = (uint32_t)zng_crc32_combine(r->crc, slot->crc, (z_off64_t)slot->out_len);
    r->total_out += slot->out_len;
    pipeline_drained(&r->pipeline);
    reader_handout(r, slot->out, slot->out_len, slot);
}

/* Hand out the next block in order. */
static int32_t reader_drain(gzblock_reader *r) {
    size_t index = r->pipeline.next_drain;
    slot_t *slot = pipeline_wait(&r->pipeline, index);

    if (slot->status == SEGMENT_FULL && slot->in_used == slot->in_len && !slot->last) {
        reader_block_out(r, slot);
        return 0;
    }
    if (slot->status == SEGMENT_END) {
        /* The final block. What followed it in its input, the trailer and possibly more members,
           goes back to the front of the input. */
        reader_block_out(r, slot);
        if (reader_rewind(r, slot->in + slot->in_used, slot->in_len - slot->in_used) != 0)
            return -1;
        r->state = READER_MEMBER_END;
        return 0;
    }
    if (slot->status == SEGMENT_OVERFLOW && index == 0)
        return reader_fallback(r);
    if (slot->status == SEGMENT_SHORT && !slot->last) {
        /* A chance marker cut the block short. Its input goes back to be cut again, scanning on
           from past that marker, so the block ends at the next one instead. */
        size_t skip = slot->in_len;
        if (reader_rewind(r, NULL, 0) != 0)
            return -1;
        cutter_rescan(&r->cut, skip);
        return 0;
    }
    if (slot->status == SEGMENT_FULL)
        return reader_fail(r, slot->last ? Z_BUF_ERROR : Z_DATA_ERROR,
                           slot->last ? "unexpected end of file" : "block %zu has trailing data", index);
    return reader_fail(r, slot->status == SEGMENT_SHORT ? Z_BUF_ERROR : Z_DATA_ERROR, "block %zu is %s", index,
                       segment_status_name(slot->status));
}

static int32_t reader_blocks(gzblock_reader *r) {
    if (reader_produce(r) != 0)
        return -1;
    if (r->state != READER_BLOCKS)
        return 0; /* fell back to plain inflate */
    if (pipeline_has_pending(&r->pipeline))
        return reader_drain(r);
    return reader_fail(r, Z_BUF_ERROR, "unexpected end of file");
}

/* The 8 trailer bytes follow the final block, then the next member or the end. */
static int32_t reader_member_end_step(gzblock_reader *r) {
    uint32_t want_crc, want_size;

    if (reader_fill(r, FORMAT_TRAILER_LEN) != 0)
        return -1;
    if (r->in_buf.len < FORMAT_TRAILER_LEN)
        return reader_fail(r, Z_BUF_ERROR, "unexpected end of file");
    format_trailer_parse(buf_data(&r->in_buf), &want_crc, &want_size);
    if (r->crc != want_crc)
        return reader_fail(r, Z_DATA_ERROR, "crc mismatch in the gzip trailer");
    if (want_size != (uint32_t)r->total_out)
        return reader_fail(r, Z_DATA_ERROR, "length mismatch in the gzip trailer");
    buf_drop(&r->in_buf, FORMAT_TRAILER_LEN);
    r->members++;
    r->state = READER_HEADER;
    return 0;
}

/* ===========================================================================
 * Member headers and the boundary probe
 */

/* Probe defaults when nothing declares a block size, the coalescing target and how far to look. */
#define PROBE_BLOCK  (128u << 10)
#define PROBE_WINDOW (1u << 20)

/* Look for a marker pair in the first stretch of compressed data. Returns 1 when one is there,
   0 when not, -1 on a read error already recorded. */
static int32_t reader_probe(gzblock_reader *r, size_t hdr_len) {
    const uint8_t *bp;
    size_t limit;

    if (reader_fill(r, hdr_len + PROBE_WINDOW) != 0)
        return -1;
    bp = buf_data(&r->in_buf);
    limit = MIN(r->in_buf.len, hdr_len + PROBE_WINDOW);
    if (limit < hdr_len + 9)
        return 0;
    return scan_marker_pair(bp + hdr_len, bp + limit - 8) != NULL;
}

/* Enter sized-member mode. Members are cut whole, so the pipeline needs no cutter state. */
static int32_t reader_start_members(gzblock_reader *r) {
    if (reader_pipeline_start(r, PROBE_BLOCK) != 0)
        return -1;
    r->memb.seg.len = 0;
    r->memb.count = 0;
    r->memb.done = 0;
    pipeline_reset(&r->pipeline);
    r->state = READER_MEMBERS;
    return 0;
}

/* Gather whole sized members into seg until about a block of input is in hand. Returns 1 with a
   group to submit, 0 when the input's next bytes are not a sized member, with whatever the group
   already holds, -1 on an error already recorded. */
static int32_t reader_members_next(gzblock_reader *r) {
    for (;;) {
        format_header hdr;
        size_t hdr_len;

        if (r->memb.seg.len >= (size_t)r->pipeline.pool.block_size)
            return 1;
        hdr_len = format_header_parse(buf_data(&r->in_buf), r->in_buf.len, &hdr);
        if (hdr_len != 0 && hdr_len != (size_t)-1 && hdr.member_size >= hdr_len + 8 &&
            r->in_buf.len < hdr.member_size && !r->in_eof) {
            if (reader_fill(r, hdr.member_size) != 0)
                return -1;
            continue;
        }
        if (hdr_len == 0 && !r->in_eof) {
            if (reader_fill(r, r->in_buf.len + IO_CHUNK) != 0)
                return -1;
            continue;
        }
        if (hdr_len == 0 || hdr_len == (size_t)-1 || hdr.member_size < hdr_len + 8 || r->in_buf.len < hdr.member_size) {
            r->memb.done = 1; /* the header path decides what the rest is */
            return r->memb.seg.len != 0;
        }
        if (buf_append(&r->memb.seg, buf_data(&r->in_buf), hdr.member_size) != 0)
            return reader_oom(r);
        buf_drop(&r->in_buf, hdr.member_size);
        r->memb.count++;
    }
}

/* Keep the pool fed with member groups and hand out the next one in order. */
static int32_t reader_members_step(gzblock_reader *r) {
    while (!r->memb.done) {
        slot_t *slot = pool_slot(&r->pipeline.pool, r->pipeline.next_submit);
        int32_t rc;

        if (slot->state != SLOT_FREE)
            break;
        rc = reader_members_next(r);
        if (rc < 0)
            return -1;
        if (rc == 0)
            break;
        slot_swap_in(slot, &r->memb.seg);
        slot->members = r->memb.count;
        slot->last = 0;
        slot->pair = 0;
        r->memb.count = 0;
        pipeline_submit(&r->pipeline, slot);
    }
    if (pipeline_has_pending(&r->pipeline)) {
        slot_t *slot = pipeline_wait(&r->pipeline, r->pipeline.next_drain);
        if (slot->status != SEGMENT_FULL)
            return reader_fail(r, Z_DATA_ERROR, "member %d is %s", r->members, segment_status_name(slot->status));
        r->members += slot->members;
        pipeline_drained(&r->pipeline);
        reader_handout(r, slot->out, slot->out_len, slot);
        return 0;
    }
    /* Every group is drained and the input goes on with something else. */
    r->state = READER_HEADER;
    return 0;
}

/* Decide how to decode what comes next: a gzip member in block mode or plain, pass-through for data
   that is not gzip, or the end. */
static int32_t reader_header(gzblock_reader *r) {
    size_t want = 1024, hdr_len;
    uint32_t hdr_block_size;
    format_header hdr;

    for (;;) {
        if (reader_fill(r, want) != 0)
            return -1;
        if (!format_is_gzip(buf_data(&r->in_buf), r->in_buf.len)) {
            if (r->in_buf.len == 0 && r->in_eof)
                r->state = READER_END;
            else if (r->members == 0)
                r->state = READER_PASSTHRU; /* not gzip, hand it back unchanged */
            else
                r->state = READER_END; /* trailing garbage after a member, ignored */
            return 0;
        }
        hdr_len = format_header_parse(buf_data(&r->in_buf), r->in_buf.len, &hdr);
        if (hdr_len == (size_t)-1)
            return reader_fail(r, Z_DATA_ERROR, "not in gzip format");
        if (hdr_len != 0)
            break;
        if (r->in_eof)
            return reader_fail(r, Z_BUF_ERROR, "unexpected end of file");
        /* A header that does not fit in a megabyte is not one worth buffering, inflate takes it
           piece by piece. */
        if (want >= (1u << 20)) {
            reader_start_stream(r);
            return 0;
        }
        want *= 2;
    }
    /* One thread gains nothing from blocks and pays for the scan and an extra copy, so plain
       inflate streams the member. */
    if (r->nthreads == 1) {
        reader_start_stream(r);
        return 0;
    }
    /* A sized member's end is known before anything inflates, so members decode whole and in
       parallel, with nothing probed, rewound, or inflated twice. */
    if (hdr.member_size != 0)
        return reader_start_members(r);
    /* Nothing in a header says how a member is cut, so a caller's hint decides, or the probe. */
    hdr_block_size = r->block_hint;
    /* A block size that would cost more memory than is sensible. */
    if (hdr_block_size > GZBLOCK_MAX_BLOCK) {
        reader_start_stream(r);
        return 0;
    }
    if (hdr_block_size == 0) {
        /* No declared size and no hint. An early marker pair means someone wrote independent
           chunks, the full flush behind a pair resets the dictionary, so decode them in
           parallel. Anything else inflates serially as before. */
        switch (reader_probe(r, hdr_len)) {
        case -1:
            return -1;
        case 0:
            reader_start_stream(r);
            return 0;
        }
        return reader_start_blocks(r, hdr_len, PROBE_BLOCK, 1);
    }
    return reader_start_blocks(r, hdr_len, hdr_block_size, 0);
}

/* ===========================================================================
 * The reader object
 */

gzblock_reader *gzblock_reader_open(gzblock_read_fn read, void *ctx, const uint8_t *head, size_t head_len,
                                    uint32_t block_size, int32_t nthreads) {
    gzblock_reader *r;

    if (!read)
        return NULL;
    r = (gzblock_reader *)calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    /* The plain stream opens here, which makes the first call into zlib-ng, the one that fills
       its function table, on this thread rather than on every worker at once. */
    if (zng_inflateInit2(&r->strm, MAX_WBITS + 16) != Z_OK) {
        free(r);
        return NULL;
    }
    r->read = read;
    r->ctx = ctx;
    r->block_hint = block_size > GZBLOCK_MAX_BLOCK ? 0 : block_size;
    r->nthreads = nthreads > 0 ? nthreads : pool_default_threads();
    r->out_buf = (uint8_t *)malloc(IO_CHUNK);
    if (!r->out_buf || (head_len != 0 && buf_append(&r->in_buf, head, head_len) != 0)) {
        gzblock_reader_close(r);
        return NULL;
    }
    r->state = READER_HEADER;
    return r;
}

/* Advance until there is output to hand out or the data ends. Returns 0 with r->have set or the
   state at READER_END, -1 on error. */
static int32_t reader_advance(gzblock_reader *r) {
    int32_t rc;
    while (r->have == 0) {
        /* Output handed out earlier has been consumed, its slot can go back to the pool. */
        if (r->slot) {
            pool_release(&r->pipeline.pool, r->slot);
            r->slot = NULL;
        }
        switch (r->state) {
        case READER_HEADER:
            rc = reader_header(r);
            break;
        case READER_PASSTHRU:
            rc = reader_passthru(r);
            break;
        case READER_STREAM:
            rc = reader_stream(r);
            break;
        case READER_BLOCKS:
            rc = reader_blocks(r);
            break;
        case READER_MEMBERS:
            rc = reader_members_step(r);
            break;
        case READER_MEMBER_END:
            rc = reader_member_end_step(r);
            break;
        case READER_END:
            return 0;
        default:
            return -1;
        }
        if (rc != 0)
            return -1;
    }
    return 0;
}

int32_t gzblock_reader_read(gzblock_reader *r, uint8_t *buf, size_t len, size_t *got) {
    size_t done = 0;

    while (done < len) {
        size_t n;
        if (reader_advance(r) != 0)
            return -1;
        if (r->have == 0)
            break; /* end of the data */
        n = MIN(r->have, len - done);
        memcpy(buf + done, r->next, n);
        done += n;
        r->next += n;
        r->have -= n;
    }
    *got = done;
    return 0;
}

int32_t gzblock_reader_next(gzblock_reader *r, const uint8_t **p, size_t *n) {
    if (reader_advance(r) != 0)
        return -1;
    *p = r->next;
    *n = r->have;
    /* Consumed as far as the reader is concerned, the slot goes back on the next call. */
    r->next += r->have;
    r->have = 0;
    return 0;
}

const char *gzblock_reader_error(const gzblock_reader *r) {
    return r->msg;
}

int32_t gzblock_reader_errcode(const gzblock_reader *r) {
    return r->err;
}

void gzblock_reader_close(gzblock_reader *r) {
    if (!r)
        return;
    pipeline_free(&r->pipeline);
    zng_inflateEnd(&r->strm);
    free(r->out_buf);
    cutter_free(&r->cut);
    free(r->memb.seg.p);
    free(r->hdr.p);
    free(r->in_buf.p);
    free(r);
}
