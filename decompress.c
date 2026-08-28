/* decompress.c -- decompression through the block engine reader
 * For conditions of distribution and use, see LICENSE.md
 */

#include "decompress.h"

#include "gzblock.h"

typedef struct {
    FILE *f;
    uint64_t in;
} gzblock_reader_ctx;

static size_t file_read(void *ctx, uint8_t *buf, size_t len) {
    gzblock_reader_ctx *reader_ctx = (gzblock_reader_ctx *)ctx;
    size_t n = fread(buf, 1, len, reader_ctx->f);
    if (n == 0 && ferror(reader_ctx->f))
        return (size_t)-1;
    reader_ctx->in += n;
    return n;
}

int32_t gzng_decompress_stream(FILE *in, FILE *out, const uint8_t *head, size_t head_len, uint32_t block_size,
                               int32_t threads, uint64_t *total_in, uint64_t *total_out) {
    gzblock_reader_ctx reader_ctx = {in, head_len};
    uint64_t total = 0;
    gzblock_reader *r = gzblock_reader_open(file_read, &reader_ctx, head, head_len, block_size, threads);

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
        *total_in = reader_ctx.in;
    if (total_out != NULL)
        *total_out = total;
    return 0;
}
