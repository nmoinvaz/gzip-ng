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
    FILE *f;
    uint64_t out;
} wsink;

static size_t file_write(void *ctx, const uint8_t *buf, size_t len) {
    wsink *s = (wsink *)ctx;
    s->out += len;
    return fwrite(buf, 1, len, s->f);
}

/* Compress through the block engine, independent blocks sealed with marker pairs. */
static int block_compress_stream(FILE *in, FILE *out, const gzng_options *opt, uint32_t mtime, const char *name,
                                 uint64_t *total_in, uint64_t *total_out) {
    wsink sink = {out, 0};
    uint64_t total = 0;
    gzblock_writer *w =
        gzblock_writer_open(file_write, &sink, opt->level, opt->strategy, opt->block_size, opt->threads);
    uint8_t *buf = (uint8_t *)malloc(CHUNK);
    int rc = -1;

    if (w == NULL || buf == NULL)
        goto done;
    if (mtime != 0 || name != NULL)
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
    if (w != NULL)
        gzblock_writer_close(w);
    free(buf);
    if (total_in != NULL)
        *total_in = total;
    if (total_out != NULL)
        *total_out = sink.out;
    return rc;
}

/* Mean bytes between the sync points --rsyncable emits into a plain stream. */
/* How far apart --rsyncable puts its sync points in a plain stream, the spacing gzip and pigz
   both use. These are Z_SYNC_FLUSH points that let rsync resynchronise after an edit. They do
   not reset the dictionary and they do not start a block, so they cost a little ratio and
   nothing else. Block boundaries are a separate matter, spaced by --blocksize. */
#define RSYNC_SPAN 4096

/* Push one span of input through deflate and write everything it produces. */
static int deflate_span(zng_stream *strm, FILE *out, uint8_t *obuf, const uint8_t *in, size_t len, int flush) {
    int err;

    strm->next_in = (z_const uint8_t *)in;
    strm->avail_in = (uint32_t)len;
    do {
        strm->next_out = obuf;
        strm->avail_out = CHUNK;
        err = zng_deflate(strm, flush);
        if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR)
            return -1;
        if (fwrite(obuf, 1, CHUNK - strm->avail_out, out) != CHUNK - strm->avail_out)
            return -1;
    } while (strm->avail_in != 0 || strm->avail_out == 0);
    return 0;
}

/* How much of buf to feed next. Without --rsyncable that is all of it. With it the span ends
   at the next rolling hash hit, where *sync asks for a flush. */
static size_t next_span(int rsyncable, uint32_t *hash, uint32_t mask, const uint8_t *buf, size_t len, int *sync) {
    uint32_t h = *hash;
    size_t k;

    *sync = 0;
    if (!rsyncable)
        return len;
    for (k = 0; k < len; k++) {
        ROLLING_ADD(h, buf[k]);
        if (ROLLING_HIT(h, mask)) {
            *hash = h;
            *sync = 1;
            return k + 1;
        }
    }
    *hash = h;
    return len;
}

int gzng_compress_stream(FILE *in, FILE *out, const gzng_options *opt, uint32_t mtime, const char *name,
                         uint64_t *total_in, uint64_t *total_out) {
    zng_gz_header head;

    /* Threads need blocks to work on, so --processes implies one. Without either this stays a
       single deflate stream. */
    if (opt->block_size != 0 || (opt->threads_given && opt->threads != 1)) {
        gzng_options blocked = *opt;
        if (blocked.block_size == 0)
            blocked.block_size = GZNG_DEFAULT_BLOCK;
        return block_compress_stream(in, out, &blocked, mtime, name, total_in, total_out);
    }

    zng_stream strm;
    uint8_t *bufs = (uint8_t *)malloc(2 * CHUNK);
    uint8_t *ibuf = bufs, *obuf = bufs ? bufs + CHUNK : NULL;
    uint32_t hash = 0, mask = rolling_mask(RSYNC_SPAN);
    int rc = -1;

    if (bufs == NULL)
        return -1;
    memset(&strm, 0, sizeof(strm));
    if (zng_deflateInit2(&strm, opt->level, Z_DEFLATED, MAX_WBITS + 16, 8, opt->strategy) != Z_OK) {
        free(bufs);
        return -1;
    }
    /* Always set the header, even with nothing to record in it. Left to itself zlib-ng stamps
       its own OS code, which on some platforms is a value RFC 1952 does not define. */
    memset(&head, 0, sizeof(head));
    head.time = mtime;
    head.name = (uint8_t *)(uintptr_t)name;
    head.os = OS_CODE;
    zng_deflateSetHeader(&strm, &head);
    for (;;) {
        size_t have = fread(ibuf, 1, CHUNK, in), pos = 0;
        int final;

        if (ferror(in))
            goto done;
        final = have < CHUNK;
        /* An empty read still runs once, which is what emits Z_FINISH. */
        do {
            int sync;
            size_t span = next_span(opt->rsyncable, &hash, mask, ibuf + pos, have - pos, &sync);
            int flush = Z_NO_FLUSH;

            pos += span;
            if (final && pos == have)
                flush = Z_FINISH;
            else if (sync)
                flush = Z_SYNC_FLUSH;
            if (deflate_span(&strm, out, obuf, ibuf + pos - span, span, flush) != 0)
                goto done;
        } while (pos < have);
        if (final)
            break;
    }
    rc = 0;
done:
    if (total_in != NULL)
        *total_in = (uint64_t)strm.total_in;
    if (total_out != NULL)
        *total_out = (uint64_t)strm.total_out;
    zng_deflateEnd(&strm);
    free(bufs);
    return rc;
}
