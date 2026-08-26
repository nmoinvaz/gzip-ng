/* compress.c -- serial gzip compression
 * For conditions of distribution and use, see LICENSE.md
 */

#include "compress.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gzblock.h"
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
static int block_compress_stream(FILE *in, FILE *out, const gzng_options *opt,
                                 uint32_t mtime, const char *name,
                                 uint64_t *in_len, uint64_t *out_len) {
    wsink sink = {out, 0};
    uint64_t total_in = 0;
    gzblock_writer *w = gzblock_wopen(file_write, &sink, opt->level, opt->strategy,
                                      opt->block_size, opt->threads);
    uint8_t *buf = (uint8_t *)malloc(CHUNK);
    int rc = -1;

    if (w == NULL || buf == NULL)
        goto done;
    if (mtime != 0 || name != NULL)
        gzblock_wmeta(w, mtime, name);
    for (;;) {
        size_t n = fread(buf, 1, CHUNK, in);
        if (n == 0)
            break;
        total_in += n;
        if (gzblock_write(w, buf, n) != 0)
            goto engine_error;
    }
    if (ferror(in))
        goto done;
    if (gzblock_wfinish(w) != 0)
        goto engine_error;
    rc = 0;
    goto done;
engine_error:
    fprintf(stderr, "gzip-ng: %s\n", gzblock_werror(w));
done:
    if (w != NULL)
        gzblock_wclose(w);
    free(buf);
    if (in_len != NULL)
        *in_len = total_in;
    if (out_len != NULL)
        *out_len = sink.out;
    return rc;
}

int gzng_compress_stream(FILE *in, FILE *out, const gzng_options *opt,
                         uint32_t mtime, const char *name,
                         uint64_t *in_len, uint64_t *out_len) {
    zng_gz_header head;

    if (opt->block_size != 0)
        return block_compress_stream(in, out, opt, mtime, name, in_len, out_len);

    zng_stream z;
    uint8_t *bufs = (uint8_t *)malloc(2 * CHUNK);
    uint8_t *ibuf = bufs, *obuf = bufs ? bufs + CHUNK : NULL;
    int err = Z_OK, flush = Z_NO_FLUSH, rc = -1;

    if (bufs == NULL)
        return -1;
    memset(&z, 0, sizeof(z));
    if (zng_deflateInit2(&z, opt->level, Z_DEFLATED, 15 + 16, 8, opt->strategy) != Z_OK) {
        free(bufs);
        return -1;
    }
    if (mtime != 0 || name != NULL) {
        memset(&head, 0, sizeof(head));
        head.time = mtime;
        head.name = (uint8_t *)(uintptr_t)name;
#ifndef _WIN32
        head.os = 3;
#endif
        zng_deflateSetHeader(&z, &head);
    }
    while (err != Z_STREAM_END) {
        if (z.avail_in == 0 && flush == Z_NO_FLUSH) {
            z.avail_in = (uint32_t)fread(ibuf, 1, CHUNK, in);
            z.next_in = ibuf;
            if (ferror(in))
                goto done;
            if (z.avail_in < CHUNK)
                flush = Z_FINISH;
        }
        z.next_out = obuf;
        z.avail_out = CHUNK;
        err = zng_deflate(&z, flush);
        if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR)
            goto done;
        if (fwrite(obuf, 1, CHUNK - z.avail_out, out) != CHUNK - z.avail_out)
            goto done;
    }
    rc = 0;
done:
    if (in_len != NULL)
        *in_len = (uint64_t)z.total_in;
    if (out_len != NULL)
        *out_len = (uint64_t)z.total_out;
    zng_deflateEnd(&z);
    free(bufs);
    return rc;
}
