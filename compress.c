/* compress.c -- serial gzip compression
 * For conditions of distribution and use, see LICENSE.md
 */

#include "compress.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "format.h"
#include "gzblock.h"
#include "rolling.h"
#include "zlib-ng.h"

#define CHUNK (256 * 1024)

typedef struct {
    FILE *out;
    uint64_t total_out;
} gzblock_writer_ctx;

static size_t file_write(void *ctx, const uint8_t *buf, size_t len) {
    gzblock_writer_ctx *writer_ctx = (gzblock_writer_ctx *)ctx;
    writer_ctx->total_out += len;
    return fwrite(buf, 1, len, writer_ctx->out);
}

/* Compress through the block engine, independent blocks sealed with marker pairs. */
static int32_t block_compress_stream(FILE *in, FILE *out, int32_t level, int32_t strategy, uint32_t block_size,
                                     int32_t threads, uint32_t mtime, const char *name, uint64_t *total_in,
                                     uint64_t *total_out) {
    gzblock_writer_ctx writer_ctx = {out, 0};
    uint64_t total = 0;
    gzblock_writer *w = gzblock_writer_open(file_write, &writer_ctx, level, strategy, block_size, threads);
    uint8_t *buf = (uint8_t *)malloc(CHUNK);
    int32_t rc = -1;

    if (!w || !buf)
        goto done;
    if (mtime != 0 || name)
        gzblock_writer_meta(w, mtime, name);
    /* Boundaries always follow the content. It costs 0.02% of the output and keeps an edit
       local, so it is not worth putting behind a flag. */
    gzblock_writer_rsyncable(w, 1);
    for (;;) {
        size_t n = fread(buf, 1, CHUNK, in);
        if (n == 0)
            break;
        total += n;
        if (gzblock_writer_write(w, buf, n) != 0)
            goto engine_error;
    }
    if (ferror(in))
        goto done;
    if (gzblock_writer_finish(w) != 0)
        goto engine_error;
    rc = 0;
    goto done;
engine_error:
    fprintf(stderr, "gzip-ng: %s\n", gzblock_writer_error(w));
done:
    if (w)
        gzblock_writer_close(w);
    free(buf);
    if (total_in)
        *total_in = total;
    if (total_out)
        *total_out = writer_ctx.total_out;
    return rc;
}

/* Mean bytes between the sync points --rsyncable emits into a plain stream. */
/* How far apart --rsyncable puts its sync points in a plain stream, the spacing gzip and pigz
   both use. These are Z_SYNC_FLUSH points that let rsync resynchronise after an edit. They do
   not reset the dictionary and they do not start a block, so they cost a little ratio and
   nothing else. Block boundaries are a separate matter, spaced by --blocksize. */
#define RSYNC_SPAN 4096

/* Push one span of input through deflate and write everything it produces. */
static int32_t deflate_span(zng_stream *strm, FILE *out, uint8_t *out_buf, const uint8_t *in, size_t len,
                            int32_t flush) {
    int32_t err;

    strm->next_in = (z_const uint8_t *)in;
    strm->avail_in = (uint32_t)len;
    do {
        strm->next_out = out_buf;
        strm->avail_out = CHUNK;
        err = zng_deflate(strm, flush);
        if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR)
            return -1;
        if (fwrite(out_buf, 1, CHUNK - strm->avail_out, out) != CHUNK - strm->avail_out)
            return -1;
    } while (strm->avail_in != 0 || strm->avail_out == 0);
    return 0;
}

int32_t gzng_compress_stream(FILE *in, FILE *out, int32_t level, int32_t strategy, uint32_t block_size, int32_t threads,
                             int32_t rsyncable, uint32_t mtime, const char *name, uint64_t *total_in,
                             uint64_t *total_out) {
    zng_gz_header head;

    if (block_size != 0)
        return block_compress_stream(in, out, level, strategy, block_size, threads, mtime, name, total_in, total_out);

    zng_stream strm;
    uint8_t *buffers = (uint8_t *)malloc(2 * CHUNK);
    uint8_t *in_buf = buffers;
    uint8_t *out_buf = buffers ? buffers + CHUNK : NULL;
    uint32_t rsync_hash = 0;
    uint32_t rsync_mask = rolling_mask(RSYNC_SPAN);
    int32_t rc = -1;

    if (!buffers)
        return -1;
    memset(&strm, 0, sizeof(strm));
    if (zng_deflateInit2(&strm, level, Z_DEFLATED, MAX_WBITS + 16, 8, strategy) != Z_OK) {
        free(buffers);
        return -1;
    }
    /* Always set the header, even with nothing to record in it. Left to itself zlib-ng stamps
       its own OS code, which on some platforms is a value RFC 1952 does not define. */
    memset(&head, 0, sizeof(head));
    head.time = mtime;
    head.name = (uint8_t *)(uintptr_t)name;
    head.os = FORMAT_OS_CODE;
    zng_deflateSetHeader(&strm, &head);
    for (;;) {
        size_t have = fread(in_buf, 1, CHUNK, in), pos = 0;
        int32_t final;

        if (ferror(in))
            goto done;
        final = have < CHUNK;
        /* An empty read still runs once, which is what emits Z_FINISH. */
        do {
            size_t span = have - pos;
            int32_t flush = Z_NO_FLUSH;

            /* With --rsyncable the span ends at the next rolling hash hit, with a flush there. */
            if (rsyncable) {
                size_t hit = rolling_find(&rsync_hash, rsync_mask, in_buf + pos, span, 0);
                if (hit < span) {
                    span = hit + 1;
                    flush = Z_SYNC_FLUSH;
                }
            }
            pos += span;
            if (final && pos == have)
                flush = Z_FINISH;
            if (deflate_span(&strm, out, out_buf, in_buf + pos - span, span, flush) != 0)
                goto done;
        } while (pos < have);
        if (final)
            break;
    }
    rc = 0;
done:
    if (total_in)
        *total_in = (uint64_t)strm.total_in;
    if (total_out)
        *total_out = (uint64_t)strm.total_out;
    zng_deflateEnd(&strm);
    free(buffers);
    return rc;
}
