/* compress.c -- serial gzip compression
 * For conditions of distribution and use, see LICENSE.md
 */

#include "compress.h"

#include <stdlib.h>
#include <string.h>

#include "zlib-ng.h"

#define CHUNK (256 * 1024)

int gzng_compress_stream(FILE *in, FILE *out, const gzng_options *opt) {
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
    zng_deflateEnd(&z);
    free(bufs);
    return rc;
}
