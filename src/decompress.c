/* decompress.c -- decompression through the block engine reader
 * For conditions of distribution and use, see LICENSE.md
 */

#include "decompress.h"

#include "gzblock.h"

static size_t file_read(void *ctx, uint8_t *buf, size_t len) {
    FILE *f = (FILE *)ctx;
    size_t n = fread(buf, 1, len, f);
    if (n == 0 && ferror(f))
        return (size_t)-1;
    return n;
}

int gzng_decompress_stream(FILE *in, FILE *out, const gzng_options *opt) {
    gzblock_reader *r = gzblock_ropen(file_read, in, NULL, 0, opt->block_size, opt->threads);

    if (r == NULL)
        return -1;
    for (;;) {
        const uint8_t *p;
        size_t n;
        if (gzblock_rnext(r, &p, &n) != 0) {
            fprintf(stderr, "gzip-ng: %s\n", gzblock_rerror(r));
            gzblock_rclose(r);
            return -1;
        }
        if (n == 0)
            break;
        if (fwrite(p, 1, n, out) != n) {
            gzblock_rclose(r);
            return -1;
        }
    }
    gzblock_rclose(r);
    return 0;
}
