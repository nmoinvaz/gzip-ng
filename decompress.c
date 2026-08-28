/* decompress.c -- decompression through the block engine reader
 * For conditions of distribution and use, see LICENSE.md
 */

#include "decompress.h"

#include "format.h"
#include "gzblock.h"

int gzng_read_meta(FILE *in, uint32_t *mtime, char *name, size_t name_len) {
    uint8_t buf[4096];
    size_t got = fread(buf, 1, sizeof(buf), in);
    format_header hdr;

    *mtime = 0;
    if (name_len != 0)
        name[0] = 0;
    if (fseek(in, 0, SEEK_SET) != 0 || !format_is_gzip(buf, got))
        return -1;
    /* A header that outruns the buffer still yields the fields that fit in it. */
    if (format_header_parse(buf, got, &hdr) == (size_t)-1)
        return -1;
    *mtime = hdr.mtime;
    if (hdr.name != NULL && name_len != 0)
        snprintf(name, name_len, "%s", hdr.name);
    return 0;
}

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
