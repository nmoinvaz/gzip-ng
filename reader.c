/* reader.c -- the parallel reader for gzip members made of independent deflate blocks
 * For conditions of distribution and use, see LICENSE.md
 */

#include <stdarg.h>

#include "gzblock_p.h"
#include "scanner.h"

enum { READER_HEADER, READER_PASSTHRU, READER_STREAM, READER_BLOCKS, READER_MEMBER_END, READER_END, READER_ERROR };

typedef struct {
    gzblock_read_fn read;
    void *ctx;
    int nthreads;
    uint32_t block_hint; /* block size to assume when a header records none */
    int state;
    int members; /* gzip members finished so far */
    int err;     /* zlib error code once failed */
    char msg[MSG_LEN];

    buf_t buf; /* input in hand, not yet consumed by the current stage */
    int eof;   /* the read callback returned 0 */

    const uint8_t *out_p; /* output being handed out */
    size_t out_n;
    slot_t *out_slot; /* slot to release once out_p is consumed, or NULL */
} reader_io;

typedef struct {
    zng_stream z; /* plain inflate */
    int z_init;
    uint8_t *obuf; /* IO_CHUNK, output of z, or bytes passed through */
} reader_inflate;

struct gzblock_reader_s {
    reader_io io;
    reader_inflate stream;


    uint32_t block_size; /* block mode */
    int paired;          /* boundaries are marker pairs, lone markers are not candidates */
    int pair_seen;       /* a pair turned up in this member, so treat it as pair-delimited */
    size_t max_seg;      /* how much the reader will hold looking for one boundary */
    buf_t hdr;           /* this member's header, kept for the fallback */
    size_t scanned;      /* bytes of buf already scanned for markers */
    size_t coal;         /* rightmost pair end while coalescing small chunks, 0 when not */
    int cut_all;         /* the scanner handed out the member's last segment */
    buf_t seg;           /* segment most recently cut out of buf */
    int seg_last;
    int seg_pair; /* the segment ends with a marker pair */
    size_t next_produce, next_emit;
    pool_t pool;
    int pool_up;
    zng_stream mz; /* for repairing false splits on this thread */
    size_t tmp_cap;
    int mz_init;
    uint8_t *tmp; /* repaired and final blocks, block_size until one needs more */
    uint32_t crc; /* running crc and length of the member's output */
    size_t total;
};

/* ===========================================================================
 * Errors, input, and output handoff
 */

static int reader_fail(gzblock_reader *r, int err, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->io.msg, sizeof(r->io.msg), fmt, ap);
    va_end(ap);
    r->io.err = err;
    r->io.state = READER_ERROR;
    return -1;
}

static int reader_fill(gzblock_reader *r, size_t want) {
    if (buf_fill(&r->io.buf, r->io.read, r->io.ctx, want, &r->io.eof) != 0)
        return reader_fail(r, Z_ERRNO, "read error");
    return 0;
}

static int reader_oom(gzblock_reader *r) {
    return reader_fail(r, Z_MEM_ERROR, "out of memory");
}

static void reader_handout(gzblock_reader *r, const uint8_t *p, size_t n, slot_t *slot) {
    r->io.out_p = p;
    r->io.out_n = n;
    r->io.out_slot = slot;
}

/* ===========================================================================
 * Plain inflate and pass-through
 */

/* Plain inflate of a member, starting with whatever is in buf. */
static int reader_start_stream(gzblock_reader *r) {
    if (!r->stream.z_init) {
        memset(&r->stream.z, 0, sizeof(r->stream.z));
        if (zng_inflateInit2(&r->stream.z, MAX_WBITS + 16) != Z_OK)
            return reader_oom(r);
        r->stream.z_init = 1;
    } else {
        zng_inflateReset(&r->stream.z);
    }
    r->io.state = READER_STREAM;
    return 0;
}

static int reader_stream(gzblock_reader *r) {
    size_t feed;
    int err;

    if (r->io.buf.len == 0) {
        if (reader_fill(r, 1) != 0)
            return -1;
        if (r->io.buf.len == 0)
            return reader_fail(r, Z_BUF_ERROR, "unexpected end of file");
    }
    feed = clamp_u32(r->io.buf.len);
    r->stream.z.next_in = (z_const uint8_t *)buf_data(&r->io.buf);
    r->stream.z.avail_in = (uint32_t)feed;
    r->stream.z.next_out = r->stream.obuf;
    r->stream.z.avail_out = IO_CHUNK;
    err = zng_inflate(&r->stream.z, Z_NO_FLUSH);
    buf_drop(&r->io.buf, feed - r->stream.z.avail_in);
    if (err != Z_OK && err != Z_STREAM_END)
        return reader_fail(r, Z_DATA_ERROR, "inflate failed: %s", r->stream.z.msg ? r->stream.z.msg : "data error");
    reader_handout(r, r->stream.obuf, IO_CHUNK - r->stream.z.avail_out, NULL);
    if (err == Z_STREAM_END) {
        r->io.members++;
        r->io.state = READER_HEADER;
    }
    return 0;
}

/* Not gzip at all, copied through unchanged. */
static int reader_passthru(gzblock_reader *r) {
    size_t n;
    if (r->io.buf.len != 0) {
        n = MIN(r->io.buf.len, IO_CHUNK);
        memcpy(r->stream.obuf, buf_data(&r->io.buf), n);
        buf_drop(&r->io.buf, n);
        reader_handout(r, r->stream.obuf, n, NULL);
        return 0;
    }
    if (r->io.eof) {
        r->io.state = READER_END;
        return 0;
    }
    n = r->io.read(r->io.ctx, r->stream.obuf, IO_CHUNK);
    if (n == (size_t)-1)
        return reader_fail(r, Z_ERRNO, "read error");
    if (n == 0) {
        r->io.eof = 1;
        r->io.state = READER_END;
        return 0;
    }
    reader_handout(r, r->stream.obuf, n, NULL);
    return 0;
}

/* ===========================================================================
 * Cutting segments out of the input
 */

/* Move the first n bytes of the input buffer into seg. */
static int reader_cut(gzblock_reader *r, size_t n, int last, int pair) {
    r->seg.len = 0;
    if (buf_append(&r->seg, buf_data(&r->io.buf), n) != 0)
        return reader_oom(r);
    r->seg_last = last;
    r->seg_pair = pair;
    buf_drop(&r->io.buf, n);
    r->scanned = 0;
    r->coal = 0;
    return 0;
}

/* Cut the next candidate segment out of the input into seg. Returns 1 when there is one, 0 once the
   input is used up, -1 on an error already recorded, -2 when the data in hand is longer than any
   block could be. */
static int reader_next_segment(gzblock_reader *r) {
    buf_t *b = &r->io.buf;

    for (;;) {
        size_t limit = b->len >= 3 ? b->len - 3 : 0;
        const uint8_t *bp = buf_data(b);
        const uint8_t *hit = r->scanned < limit ? scan_marker(bp + r->scanned, bp + limit) : NULL;
        if (hit != NULL) {
            size_t n = (size_t)(hit - bp) + 4;
            int empties = 0;
            /* Empty stored blocks right after the marker belong to this segment too, the second one
               is what makes a boundary when the header says pairs. Their bytes must be in hand. */
            for (;;) {
                if (n + 5 > b->len) {
                    if (r->io.eof)
                        break;
                    r->scanned = (size_t)(hit - bp);
                    goto read_more;
                }
                if (memcmp(bp + n, "\0\0\0\xff\xff", 5) != 0)
                    break;
                n += 5;
                empties++;
            }
            if (empties > 0)
                r->pair_seen = 1;
            if ((r->paired || r->pair_seen) && empties == 0) {
                /* a lone marker, a flush inside a block or data that happens to match */
                r->scanned = (size_t)(hit - bp) + 1;
                continue;
            }
            if (empties > 0 && n < r->block_size) {
                /* A small pair-terminated chunk. One slot per chunk costs more in handoff than
                   inflation, so keep absorbing chunks until a block of input is in hand. */
                r->coal = n;
                r->scanned = n;
                continue;
            }
            if (n > r->max_seg) {
                if (r->coal != 0)
                    return reader_cut(r, r->coal, 0, 1) != 0 ? -1 : 1;
                return -2;
            }
            return reader_cut(r, n, 0, empties > 0) != 0 ? -1 : 1;
        }
        r->scanned = limit;
        if (b->len > r->max_seg + 3) {
            if (r->coal != 0)
                return reader_cut(r, r->coal, 0, 1) != 0 ? -1 : 1;
            return -2;
        }
        if (r->io.eof) {
            if (b->len == 0)
                return 0;
            return reader_cut(r, b->len, 1, 0) != 0 ? -1 : 1;
        }
    read_more:
        if (reader_fill(r, b->len + IO_CHUNK) != 0)
            return -1;
    }
}

/* ===========================================================================
 * The block pipeline
 */

/* Enter block mode for a member whose header (the first hdr_len bytes of buf) records, or -b
   supplies, a block size. */
static int reader_start_blocks(gzblock_reader *r, size_t hdr_len, uint32_t block_size, int paired) {
    r->hdr.len = 0;
    if (buf_append(&r->hdr, buf_data(&r->io.buf), hdr_len) != 0)
        return reader_oom(r);
    buf_drop(&r->io.buf, hdr_len);

    if (r->pool_up && r->block_size != block_size) {
        pool_stop(&r->pool);
        pool_free(&r->pool);
        r->pool_up = 0;
        free(r->tmp);
        r->tmp = NULL;
    }
    r->block_size = block_size;
    /* A pair-terminated block may be any size and coalescing gathers several, so this is a
       memory bound rather than a property of the format. */
    r->max_seg = (size_t)block_size * 4 + 1024;
    if (!r->pool_up) {
        r->pool.codec.init = gzblock_codec_init;
        r->pool.codec.end = gzblock_codec_end;
        r->pool.codec.run = gzblock_codec_run;
        r->pool.mode = POOL_INFLATE;
        r->pool.block_size = block_size;
        /* Segments are swapped in from the scanner, so slots start without an in buffer. */
        r->tmp = (uint8_t *)malloc(block_size);
        r->tmp_cap = block_size;
        if (r->tmp == NULL || pool_alloc(&r->pool, r->io.nthreads, 0, block_size) != 0)
            return reader_oom(r);
        if (pool_start(&r->pool, r->io.nthreads) != 0)
            return reader_fail(r, Z_MEM_ERROR, "cannot start threads");
        r->pool_up = 1;
    }
    if (!r->mz_init) {
        memset(&r->mz, 0, sizeof(r->mz));
        if (zng_inflateInit2(&r->mz, -MAX_WBITS) != Z_OK)
            return reader_oom(r);
        r->mz_init = 1;
    }
    r->paired = paired;
    r->scanned = 0;
    r->coal = 0;
    r->pair_seen = 0;
    r->cut_all = 0;
    r->next_produce = r->next_emit = 0;
    r->crc = 0;
    r->total = 0;
    r->io.state = READER_BLOCKS;
    return 0;
}

/* Put the header, the segments cut so far, and the input in hand back together and stream the member
   through plain inflate instead. Only valid before any of its output was handed out. */
static int reader_fallback(gzblock_reader *r) {
    buf_t all = {NULL, 0, 0, 0};
    size_t i;

    if (buf_append(&all, r->hdr.p, r->hdr.len) != 0)
        return reader_oom(r);
    for (i = r->next_emit; i < r->next_produce; i++) {
        slot_t *slot = pool_slot(&r->pool, i);
        pool_wait(&r->pool, slot);
        if (buf_append(&all, slot->in, slot->in_len) != 0)
            return reader_oom(r);
        pool_release(&r->pool, slot);
    }
    if (buf_append(&all, buf_data(&r->io.buf), r->io.buf.len) != 0)
        return reader_oom(r);
    free(r->io.buf.p);
    r->io.buf = all;
    r->hdr.len = 0;
    r->next_produce = r->next_emit = 0;
    r->scanned = 0;
    r->cut_all = 0;
    return reader_start_stream(r);
}

/* Keep the pool fed, cutting segments into free slots until the ring is full or the input is done. */
static int reader_produce(gzblock_reader *r) {
    while (!r->cut_all) {
        slot_t *slot = pool_slot(&r->pool, r->next_produce);
        buf_t swap;
        int rc;

        if (slot->state != SLOT_FREE)
            break;
        rc = reader_next_segment(r);
        if (rc == 0) {
            r->cut_all = 1;
            break;
        }
        if (rc == -1)
            return -1;
        if (rc == -2) {
            /* The blocks are larger than assumed, which happens when the probe's guess was low.
               A pair-terminated block is valid at any size, so raise the bound and retry rather
               than fail the member. */
            if (r->pair_seen && r->max_seg < GZBLOCK_MAX_BLOCK) {
                r->max_seg = MIN(r->max_seg * 2, (size_t)GZBLOCK_MAX_BLOCK);
                continue;
            }
            if (r->next_produce == 0 && r->next_emit == 0)
                return reader_fallback(r); /* no block structure at this size */
            return reader_fail(r, Z_DATA_ERROR, "block %zu is larger than the block size", r->next_produce);
        }
        /* Swap buffers rather than copy, the slot keeps the segment and seg reuses the old one. */
        swap.p = slot->in;
        swap.capacity = slot->in_cap;
        slot->in = r->seg.p;
        slot->in_cap = r->seg.capacity;
        slot->in_len = r->seg.len;
        slot->last = r->seg_last;
        slot->pair = r->seg_pair;
        r->seg.p = swap.p;
        r->seg.capacity = swap.capacity;
        r->seg.len = 0;
        pool_submit(&r->pool, slot);
        r->next_produce++;
    }
    return 0;
}

/* The member's final block was inflated. rest is what followed it in its piece, the trailer and
   possibly more members, which together with any segments cut after it and the input in hand goes
   back to the front of the input. slot, if not NULL, held rest and is released afterwards. */
static int reader_member_end(gzblock_reader *r, const uint8_t *rest, size_t rest_n, slot_t *slot) {
    buf_t all = {NULL, 0, 0, 0};
    size_t i;

    if (buf_append(&all, rest, rest_n) != 0)
        return reader_oom(r);
    if (slot != NULL)
        pool_release(&r->pool, slot);
    for (i = r->next_emit; i < r->next_produce; i++) {
        slot_t *s = pool_slot(&r->pool, i);
        pool_wait(&r->pool, s);
        if (buf_append(&all, s->in, s->in_len) != 0)
            return reader_oom(r);
        pool_release(&r->pool, s);
    }
    if (buf_append(&all, buf_data(&r->io.buf), r->io.buf.len) != 0)
        return reader_oom(r);
    free(r->io.buf.p);
    r->io.buf = all;
    r->scanned = 0;
    r->cut_all = 0;
    r->next_produce = r->next_emit = 0;
    r->io.state = READER_MEMBER_END;
    return 0;
}

/* Hand out a finished block's output and account for it. */
static void reader_block_out(gzblock_reader *r, const uint8_t *out, size_t out_len, uint32_t crc, slot_t *slot) {
    r->crc = (uint32_t)zng_crc32_combine(r->crc, crc, (z_off64_t)out_len);
    r->total += out_len;
    reader_handout(r, out, out_len, slot);
}

/* ===========================================================================
 * False marker repair and fallback
 */

/* A false marker split the block in first. Inflate it again from there on this thread, feeding the
   following pieces until the real block completes. */
static int reader_repair(gzblock_reader *r, slot_t *first) {
    block_dec m;
    const uint8_t *piece = first->in;
    size_t piece_len = first->in_len, used;
    int last = first->last, pair = first->pair, status;
    slot_t *ps = first;

    blockdec_begin(&m, &r->mz, r->tmp, r->block_size);
    for (;;) {
        m.accept_partial = pair;
        status = blockdec_feed(&m, piece, piece_len, &used);
        r->next_emit++;
        if (status == SEG_SHORT) {
            if (ps != NULL)
                pool_release(&r->pool, ps);
            if (last)
                return reader_fail(r, Z_BUF_ERROR, "block %zu is truncated", r->next_emit - 1);
            if (r->next_emit < r->next_produce) {
                /* The next piece is already in the ring, wait for its worker and take it from there. */
                ps = pool_slot(&r->pool, r->next_emit);
                pool_wait(&r->pool, ps);
                piece = ps->in;
                piece_len = ps->in_len;
                last = ps->last;
                pair = ps->pair;
            } else {
                /* Not cut yet, take it straight from the input, it never needs a slot. */
                int rc = reader_next_segment(r);
                if (rc == -1)
                    return -1;
                if (rc != 1)
                    return reader_fail(r, rc == 0 ? Z_BUF_ERROR : Z_DATA_ERROR,
                                       rc == 0 ? "unexpected end of file" : "block %zu is larger than the block size",
                                       r->next_emit);
                ps = NULL;
                piece = r->seg.p;
                piece_len = r->seg.len;
                last = r->seg_last;
                pair = r->seg_pair;
                r->next_produce++;
            }
            continue;
        }
        if (status == SEG_END) {
            reader_block_out(r, r->tmp, (size_t)r->mz.total_out,
                             (uint32_t)zng_crc32_z(0, r->tmp, (size_t)r->mz.total_out), NULL);
            return reader_member_end(r, piece + used, piece_len - used, ps);
        }
        if (status == SEG_FULL && used == piece_len && !last) {
            reader_block_out(r, r->tmp, (size_t)r->mz.total_out,
                             (uint32_t)zng_crc32_z(0, r->tmp, (size_t)r->mz.total_out), NULL);
            if (ps != NULL)
                pool_release(&r->pool, ps);
            return 0;
        }
        if (ps != NULL)
            pool_release(&r->pool, ps);
        if (status == SEG_FULL)
            return reader_fail(r, last ? Z_BUF_ERROR : Z_DATA_ERROR,
                               last ? "unexpected end of file" : "block %zu has trailing data", r->next_emit - 1);
        return reader_fail(r, status == SEG_SHORT ? Z_BUF_ERROR : Z_DATA_ERROR, "block %zu is %s", r->next_emit - 1,
                           blockdec_status_name(status));
    }
}

/* ===========================================================================
 * Draining blocks and ending a member
 */

/* Hand out the next block in order. */
static int reader_drain(gzblock_reader *r) {
    slot_t *slot = pool_slot(&r->pool, r->next_emit);

    pool_wait(&r->pool, slot);
    if (slot->status == SEG_FULL && slot->in_used == slot->in_len && !slot->last) {
        r->next_emit++;
        reader_block_out(r, slot->out, slot->out_len, slot->crc, slot);
        return 0;
    }
    if (slot->status == SEG_END) {
        /* The final block. Its output goes out from tmp so the slot can be recycled right away. */
        if (slot->out_len > r->tmp_cap) {
            uint8_t *grown = (uint8_t *)realloc(r->tmp, slot->out_len);
            if (grown == NULL)
                return reader_oom(r);
            r->tmp = grown;
            r->tmp_cap = slot->out_len;
        }
        memcpy(r->tmp, slot->out, slot->out_len);
        reader_block_out(r, r->tmp, slot->out_len, slot->crc, NULL);
        r->next_emit++;
        return reader_member_end(r, slot->in + slot->in_used, slot->in_len - slot->in_used, slot);
    }
    if (slot->status == SEG_OVERFLOW && r->next_emit == 0)
        return reader_fallback(r);
    if (slot->status == SEG_SHORT && !slot->last)
        return reader_repair(r, slot);
    if (slot->status == SEG_FULL)
        return reader_fail(r, slot->last ? Z_BUF_ERROR : Z_DATA_ERROR,
                           slot->last ? "unexpected end of file" : "block %zu has trailing data", r->next_emit);
    return reader_fail(r, slot->status == SEG_SHORT ? Z_BUF_ERROR : Z_DATA_ERROR, "block %zu is %s", r->next_emit,
                       blockdec_status_name(slot->status));
}

static int reader_blocks(gzblock_reader *r) {
    if (reader_produce(r) != 0)
        return -1;
    if (r->io.state != READER_BLOCKS)
        return 0; /* fell back to plain inflate */
    if (r->next_emit < r->next_produce)
        return reader_drain(r);
    return reader_fail(r, Z_BUF_ERROR, "unexpected end of file");
}

/* The 8 trailer bytes follow the final block, then the next member or the end. */
static int reader_member_end_step(gzblock_reader *r) {
    uint32_t want_crc, want_size;

    if (reader_fill(r, GZ_TRAILER) != 0)
        return -1;
    if (r->io.buf.len < GZ_TRAILER)
        return reader_fail(r, Z_BUF_ERROR, "unexpected end of file");
    format_trailer_parse(buf_data(&r->io.buf), &want_crc, &want_size);
    if (r->crc != want_crc)
        return reader_fail(r, Z_DATA_ERROR, "crc mismatch in the gzip trailer");
    if (want_size != (uint32_t)r->total)
        return reader_fail(r, Z_DATA_ERROR, "length mismatch in the gzip trailer");
    buf_drop(&r->io.buf, GZ_TRAILER);
    r->io.members++;
    r->io.state = READER_HEADER;
    return 0;
}

/* Decide how to decode what comes next: a gzip member in block mode or plain, pass-through for data
   that is not gzip, or the end. */
/* ===========================================================================
 * Member headers and the boundary probe
 */

/* Probe defaults when nothing declares a block size, the coalescing target and how far to look. */
#define PROBE_BLOCK  (128u << 10)
#define PROBE_WINDOW (1u << 20)

/* Look for a marker pair in the first stretch of compressed data. Returns 1 when one is there,
   0 when not, -1 on a read error already recorded. */
static int reader_probe(gzblock_reader *r, size_t hdr_len) {
    const uint8_t *bp, *hit;
    size_t limit, pos = hdr_len;

    if (reader_fill(r, hdr_len + PROBE_WINDOW) != 0)
        return -1;
    bp = buf_data(&r->io.buf);
    limit = r->io.buf.len < hdr_len + PROBE_WINDOW ? r->io.buf.len : hdr_len + PROBE_WINDOW;
    while (pos + 9 <= limit) {
        hit = scan_marker(bp + pos, bp + limit - 3);
        if (hit == NULL)
            break;
        pos = (size_t)(hit - bp);
        if (pos + 9 <= limit && memcmp(hit + 4, "\0\0\0\xff\xff", 5) == 0)
            return 1;
        pos++;
    }
    return 0;
}

static int reader_header(gzblock_reader *r) {
    size_t want = 1024, hdr_len;
    uint32_t hdr_block_size;

    for (;;) {
        if (reader_fill(r, want) != 0)
            return -1;
        if (r->io.buf.len < 2 || buf_data(&r->io.buf)[0] != 0x1f || buf_data(&r->io.buf)[1] != 0x8b) {
            if (r->io.buf.len == 0 && r->io.eof)
                r->io.state = READER_END;
            else if (r->io.members == 0)
                r->io.state = READER_PASSTHRU; /* not gzip, hand it back unchanged */
            else
                r->io.state = READER_END; /* trailing garbage after a member, ignored */
            return 0;
        }
        hdr_len = format_header_parse(buf_data(&r->io.buf), r->io.buf.len);
        if (hdr_len == (size_t)-1)
            return reader_fail(r, Z_DATA_ERROR, "not in gzip format");
        if (hdr_len != 0)
            break;
        if (r->io.eof)
            return reader_fail(r, Z_BUF_ERROR, "unexpected end of file");
        /* A header that does not fit in a megabyte is not one worth buffering, inflate takes it
           piece by piece. */
        if (want >= (1u << 20))
            return reader_start_stream(r);
        want *= 2;
    }
    /* Nothing in a header says how a member is cut, so a caller's hint decides, or the probe. */
    hdr_block_size = r->io.block_hint;
    /* A block size that would cost more memory than is sensible. */
    if (hdr_block_size > GZBLOCK_MAX_BLOCK)
        return reader_start_stream(r);
    if (hdr_block_size == 0) {
        /* No declared size and no hint. An early marker pair means someone wrote independent
           chunks, the full flush behind a pair resets the dictionary, so decode them in
           parallel. Anything else inflates serially as before. */
        switch (reader_probe(r, hdr_len)) {
        case -1:
            return -1;
        case 0:
            return reader_start_stream(r);
        }
        return reader_start_blocks(r, hdr_len, PROBE_BLOCK, 1);
    }
    return reader_start_blocks(r, hdr_len, hdr_block_size, 0);
}

/* ===========================================================================
 * The reader object
 */

gzblock_reader *gzblock_reader_open(gzblock_read_fn read, void *ctx, const uint8_t *head, size_t head_len,
                                    uint32_t block_size, int nthreads) {
    gzblock_reader *r;

    if (read == NULL)
        return NULL;
    r = (gzblock_reader *)calloc(1, sizeof(*r));
    if (r == NULL)
        return NULL;
    r->io.read = read;
    r->io.ctx = ctx;
    r->io.block_hint = block_size > GZBLOCK_MAX_BLOCK ? 0 : block_size;
    r->io.nthreads = nthreads > 0 ? nthreads : pool_default_threads();
    r->stream.obuf = (uint8_t *)malloc(IO_CHUNK);
    if (r->stream.obuf == NULL || (head_len != 0 && buf_append(&r->io.buf, head, head_len) != 0)) {
        gzblock_reader_close(r);
        return NULL;
    }
    r->io.state = READER_HEADER;
    return r;
}

/* Output handed out earlier has been consumed, the slot holding it can go back to the pool. */
static void reader_done_pending(gzblock_reader *r) {
    if (r->io.out_n == 0 && r->io.out_slot != NULL) {
        pool_release(&r->pool, r->io.out_slot);
        r->io.out_slot = NULL;
    }
}

/* Advance until there is output to hand out or the data ends. Returns 0 with r->io.out_n set or the
   state at READER_END, -1 on error. */
static int reader_advance(gzblock_reader *r) {
    int rc;
    while (r->io.out_n == 0) {
        switch (r->io.state) {
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

int gzblock_reader_read(gzblock_reader *r, uint8_t *buf, size_t len, size_t *got) {
    size_t done = 0;

    reader_done_pending(r);
    while (done < len) {
        size_t n;
        if (reader_advance(r) != 0)
            return -1;
        if (r->io.out_n == 0)
            break; /* end of the data */
        n = r->io.out_n < len - done ? r->io.out_n : len - done;
        memcpy(buf + done, r->io.out_p, n);
        done += n;
        r->io.out_p += n;
        r->io.out_n -= n;
        reader_done_pending(r);
    }
    *got = done;
    return 0;
}

int gzblock_reader_next(gzblock_reader *r, const uint8_t **p, size_t *n) {
    reader_done_pending(r);
    if (reader_advance(r) != 0)
        return -1;
    *p = r->io.out_p;
    *n = r->io.out_n;
    /* Consumed as far as the reader is concerned, the slot goes back on the next call. */
    r->io.out_p += r->io.out_n;
    r->io.out_n = 0;
    return 0;
}

const char *gzblock_reader_error(const gzblock_reader *r) {
    return r->io.msg;
}

int gzblock_reader_errcode(const gzblock_reader *r) {
    return r->io.err;
}

void gzblock_reader_close(gzblock_reader *r) {
    if (r == NULL)
        return;
    if (r->pool_up)
        pool_stop(&r->pool);
    pool_free(&r->pool);
    if (r->stream.z_init)
        zng_inflateEnd(&r->stream.z);
    if (r->mz_init)
        zng_inflateEnd(&r->mz);
    free(r->tmp);
    free(r->stream.obuf);
    free(r->seg.p);
    free(r->hdr.p);
    free(r->io.buf.p);
    free(r);
}
