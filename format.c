/* format.c -- the bytes of the gzip header and the ZB subfield
 * For conditions of distribution and use, see LICENSE.md
 */

#include "format.h"

#include "gzblock.h"

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
