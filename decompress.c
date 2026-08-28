/* decompress.c -- decompression through the block engine reader
 * For conditions of distribution and use, see LICENSE.md
 */

#include "decompress.h"

#include "gzblock.h"

typedef struct {
    FILE *f;
    uint64_t in;
} rsource;

static size_t file_read(void *ctx, uint8_t *buf, size_t len) {
    rsource *s = (rsource *)ctx;
    size_t n = fread(buf, 1, len, s->f);
    if (n == 0 && ferror(s->f))
        return (size_t)-1;
    s->in += n;
    return n;
}

int gzng_decompress_stream(FILE *in, FILE *out, const gzng_options *opt, uint64_t *total_in, uint64_t *total_out) {
    rsource src = {in, 0};
    uint64_t total = 0;
    gzblock_reader *r = gzblock_reader_open(file_read, &src, NULL, 0, opt->block_size, opt->threads);

    if (r == NULL)
        return -1;
    for (;;) {
        const uint8_t *p;
        size_t n;
        if (gzblock_reader_next(r, &p, &n) != 0) {
            fprintf(stderr, "gzip-ng: %s\n", gzblock_reader_error(r));
            gzblock_reader_close(r);
            return -1;
        }
        if (n == 0)
            break;
        total += n;
        if (out != NULL && fwrite(p, 1, n, out) != n) {
            gzblock_reader_close(r);
            return -1;
        }
    }
    gzblock_reader_close(r);
    if (total_in != NULL)
        *total_in = src.in;
    if (total_out != NULL)
        *total_out = total;
    return 0;
}
