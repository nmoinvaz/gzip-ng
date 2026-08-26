/* decompress.c -- decompression through the block engine reader
 * For conditions of distribution and use, see LICENSE.md
 */

#include "decompress.h"

#include "gzblock.h"

#include <string.h>

int gzng_read_meta(FILE *in, uint32_t *mtime, char *name, size_t name_len) {
    uint8_t buf[4096];
    size_t got = fread(buf, 1, sizeof(buf), in), pos = 10;

    *mtime = 0;
    if (name_len != 0)
        name[0] = 0;
    if (fseek(in, 0, SEEK_SET) != 0)
        return -1;
    if (got < 10 || buf[0] != 0x1f || buf[1] != 0x8b || buf[2] != 8)
        return -1;
    *mtime = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) | ((uint32_t)buf[6] << 16) |
             ((uint32_t)buf[7] << 24);
    if (buf[3] & 4) {   /* FEXTRA */
        if (got < pos + 2)
            return 0;
        pos += 2 + (buf[pos] | ((size_t)buf[pos + 1] << 8));
    }
    if ((buf[3] & 8) && pos < got) {   /* FNAME */
        size_t i = 0;
        while (pos + i < got && buf[pos + i] != 0 && i + 1 < name_len)
            i++;
        if (pos + i < got && buf[pos + i] == 0) {
            memcpy(name, buf + pos, i);
            name[i] = 0;
        }
    }
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

int gzng_decompress_stream(FILE *in, FILE *out, const gzng_options *opt,
                           uint64_t *in_len, uint64_t *out_len) {
    rsource src = {in, 0};
    uint64_t total_out = 0;
    gzblock_reader *r = gzblock_ropen(file_read, &src, NULL, 0, opt->block_size, opt->threads);

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
        total_out += n;
        if (out != NULL && fwrite(p, 1, n, out) != n) {
            gzblock_rclose(r);
            return -1;
        }
    }
    gzblock_rclose(r);
    if (in_len != NULL)
        *in_len = src.in;
    if (out_len != NULL)
        *out_len = total_out;
    return 0;
}
