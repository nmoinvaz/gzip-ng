/* decompress.c -- decompression through the block engine reader
 * For conditions of distribution and use, see LICENSE.md
 */

#include "decompress.h"

#include "gzblock.h"

typedef struct {
    FILE *in;
    uint64_t total_in;
} gzblock_reader_ctx;

static size_t file_read(void *ctx, uint8_t *buf, size_t len) {
    gzblock_reader_ctx *reader_ctx = (gzblock_reader_ctx *)ctx;
    size_t n = fread(buf, 1, len, reader_ctx->in);
    if (n == 0 && ferror(reader_ctx->in))
        return (size_t)-1;
    reader_ctx->total_in += n;
    return n;
}

int32_t gzng_decompress_stream(FILE *in, FILE *out, const uint8_t *head, size_t head_len, uint32_t block_size,
                               int32_t threads, uint64_t *total_in, uint64_t *total_out) {
    gzblock_reader_ctx reader_ctx = {in, head_len};
    uint64_t total = 0;
    gzblock_reader *reader = gzblock_reader_open(file_read, &reader_ctx, head, head_len, block_size, threads);

    if (!reader)
        return -1;
    for (;;) {
        const uint8_t *p;
        size_t n;
        if (gzblock_reader_next(reader, &p, &n) != 0) {
            fprintf(stderr, "gzip-ng: %s\n", gzblock_reader_error(reader));
            gzblock_reader_close(reader);
            return -1;
        }
        if (n == 0)
            break;
        total += n;
        if (out && fwrite(p, 1, n, out) != n) {
            gzblock_reader_close(reader);
            return -1;
        }
    }
    gzblock_reader_close(reader);
    if (total_in)
        *total_in = reader_ctx.total_in;
    if (total_out)
        *total_out = total;
    return 0;
}
