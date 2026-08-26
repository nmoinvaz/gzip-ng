/* format.c -- the bytes of the gzip header and the ZB subfield
 * For conditions of distribution and use, see LICENSE.md
 */

#include "format.h"

#include <string.h>

#include "zlib-ng.h"

size_t format_header_build(uint8_t *buf, const format_header *hdr) {
    size_t name_len = hdr->name != NULL ? strlen(hdr->name) : 0;
    size_t n = 10;

    /* A name that does not fit is left out rather than stored cut in half. */
    if (name_len >= GZBLOCK_NAME_MAX)
        name_len = 0;

    memset(buf, 0, 10);
    buf[0] = 0x1f;
    buf[1] = 0x8b;
    buf[2] = 8;                                            /* deflate */
    buf[3] = (uint8_t)((hdr->block_size != 0 ? 4 : 0) |    /* FEXTRA */
                       (name_len != 0 ? 8 : 0));           /* FNAME */
    buf[4] = (uint8_t)hdr->mtime;
    buf[5] = (uint8_t)(hdr->mtime >> 8);
    buf[6] = (uint8_t)(hdr->mtime >> 16);
    buf[7] = (uint8_t)(hdr->mtime >> 24);
    /* Extra flags, 2 for the slowest deflate settings, 4 for the fastest. */
    buf[8] = (uint8_t)(hdr->level == 9 ? 2 :
                       (hdr->strategy >= Z_HUFFMAN_ONLY ||
                        (hdr->level >= 0 && hdr->level < 2) ? 4 : 0));
#ifdef _WIN32
    buf[9] = 0;
#else
    buf[9] = 3;                                            /* Unix */
#endif
    if (hdr->block_size != 0) {
        buf[n++] = 9;      /* XLEN, one subfield of five bytes behind its four byte header */
        buf[n++] = 0;
        buf[n++] = 'Z';
        buf[n++] = 'B';
        buf[n++] = 5;      /* LEN */
        buf[n++] = 0;
        buf[n++] = (uint8_t)hdr->block_size;
        buf[n++] = (uint8_t)(hdr->block_size >> 8);
        buf[n++] = (uint8_t)(hdr->block_size >> 16);
        buf[n++] = (uint8_t)(hdr->block_size >> 24);
        buf[n++] = (uint8_t)hdr->zb_flags;
    }
    if (name_len != 0) {
        memcpy(buf + n, hdr->name, name_len + 1);
        n += name_len + 1;
    }
    return n;
}

size_t format_header_parse(const uint8_t *buf, size_t len, uint32_t *block_size, uint32_t *zb_flags) {
    size_t pos = 10;
    uint8_t flags;

    *block_size = 0;
    *zb_flags = 0;
    if (len < 10)
        return 0;
    if (buf[0] != 0x1f || buf[1] != 0x8b || buf[2] != 8)
        return (size_t)-1;
    flags = buf[3];
    if (flags & 0xe0)
        return (size_t)-1;

    if (flags & 4) {   /* FEXTRA */
        size_t xlen, end;
        if (len < pos + 2)
            return 0;
        xlen = buf[pos] | ((size_t)buf[pos + 1] << 8);
        pos += 2;
        end = pos + xlen;
        if (len < end)
            return 0;
        while (pos + 4 <= end) {
            size_t sublen = buf[pos + 2] | ((size_t)buf[pos + 3] << 8);
            if (buf[pos] == 'Z' && buf[pos + 1] == 'B' && sublen >= 4 && pos + 4 + sublen <= end) {
                *block_size = (uint32_t)buf[pos + 4] | ((uint32_t)buf[pos + 5] << 8) |
                              ((uint32_t)buf[pos + 6] << 16) | ((uint32_t)buf[pos + 7] << 24);
                if (sublen >= 5)
                    *zb_flags = buf[pos + 8];
            }
            pos += 4 + sublen;
        }
        pos = end;
    }
    if (flags & 8) {   /* FNAME */
        while (pos < len && buf[pos] != 0)
            pos++;
        if (pos >= len)
            return 0;
        pos++;
    }
    if (flags & 16) {  /* FCOMMENT */
        while (pos < len && buf[pos] != 0)
            pos++;
        if (pos >= len)
            return 0;
        pos++;
    }
    if (flags & 2) {   /* FHCRC */
        if (len < pos + 2)
            return 0;
        pos += 2;
    }
    return pos;
}

int gzblock_parse_header(const uint8_t *buf, size_t len, size_t *hdr_len, uint32_t *block_size) {
    uint32_t zb_flags;
    size_t n = format_header_parse(buf, len, block_size, &zb_flags);
    if (n == (size_t)-1)
        return -1;
    if (n == 0)
        return 0;
    *hdr_len = n;
    return 1;
}
