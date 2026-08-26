/* format.c -- the bytes of the gzip header and trailer
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
    buf[2] = 8;                                /* deflate */
    buf[3] = (uint8_t)(name_len != 0 ? 8 : 0); /* FNAME */
    buf[4] = (uint8_t)hdr->mtime;
    buf[5] = (uint8_t)(hdr->mtime >> 8);
    buf[6] = (uint8_t)(hdr->mtime >> 16);
    buf[7] = (uint8_t)(hdr->mtime >> 24);
    /* Extra flags, 2 for the slowest deflate settings, 4 for the fastest. */
    buf[8] =
        (uint8_t)(hdr->level == 9 ? 2
                                  : (hdr->strategy >= Z_HUFFMAN_ONLY || (hdr->level >= 0 && hdr->level < 2) ? 4 : 0));
#ifdef _WIN32
    buf[9] = 0;
#else
    buf[9] = 3; /* Unix */
#endif
    if (name_len != 0) {
        memcpy(buf + n, hdr->name, name_len + 1);
        n += name_len + 1;
    }
    return n;
}

size_t format_header_parse(const uint8_t *buf, size_t len) {
    size_t pos = 10;
    uint8_t flags;

    if (len < 10)
        return 0;
    if (buf[0] != 0x1f || buf[1] != 0x8b || buf[2] != 8)
        return (size_t)-1;
    flags = buf[3];
    if (flags & 0xe0)
        return (size_t)-1;

    if (flags & 4) { /* FEXTRA */
        size_t xlen, end;
        if (len < pos + 2)
            return 0;
        xlen = buf[pos] | ((size_t)buf[pos + 1] << 8);
        pos += 2;
        end = pos + xlen;
        if (len < end)
            return 0;
        pos = end;
    }
    if (flags & 8) { /* FNAME */
        while (pos < len && buf[pos] != 0)
            pos++;
        if (pos >= len)
            return 0;
        pos++;
    }
    if (flags & 16) { /* FCOMMENT */
        while (pos < len && buf[pos] != 0)
            pos++;
        if (pos >= len)
            return 0;
        pos++;
    }
    if (flags & 2) { /* FHCRC */
        if (len < pos + 2)
            return 0;
        pos += 2;
    }
    return pos;
}

void format_trailer_build(uint8_t *buf, uint32_t crc, uint64_t total) {
    buf[0] = (uint8_t)crc;
    buf[1] = (uint8_t)(crc >> 8);
    buf[2] = (uint8_t)(crc >> 16);
    buf[3] = (uint8_t)(crc >> 24);
    buf[4] = (uint8_t)total;
    buf[5] = (uint8_t)(total >> 8);
    buf[6] = (uint8_t)(total >> 16);
    buf[7] = (uint8_t)(total >> 24);
}

void format_trailer_parse(const uint8_t *buf, uint32_t *crc, uint32_t *total) {
    *crc = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    *total = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) | ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
}
