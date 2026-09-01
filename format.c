/* format.c -- the bytes of the gzip header and trailer
 * For conditions of distribution and use, see LICENSE.md
 */

#include "format.h"

#include <string.h>

#include "util.h"
#include "zlib-ng.h"

int32_t format_is_gzip(const uint8_t *buf, size_t len) {
    return len >= 2 && buf[0] == 0x1f && buf[1] == 0x8b;
}

size_t format_header_build(uint8_t *buf, const format_header *hdr) {
    size_t name_len = hdr->name ? strlen(hdr->name) : 0;
    size_t n = FORMAT_HEADER_LEN;

    /* A name that does not fit is omitted rather than truncated. */
    if (name_len >= FORMAT_NAME_MAX)
        name_len = 0;

    memset(buf, 0, FORMAT_HEADER_LEN);
    buf[0] = 0x1f;
    buf[1] = 0x8b;
    buf[2] = 8;                                /* deflate */
    buf[3] = (uint8_t)(name_len != 0 ? 8 : 0); /* FNAME */
    store_le32(buf + 4, hdr->mtime);
    /* Extra flags, 2 for the slowest deflate settings, 4 for the fastest. */
    buf[8] =
        (uint8_t)(hdr->level == 9 ? 2
                                  : (hdr->strategy >= Z_HUFFMAN_ONLY || (hdr->level >= 0 && hdr->level < 2) ? 4 : 0));
    buf[9] = FORMAT_OS_CODE;
    if (name_len != 0) {
        memcpy(buf + n, hdr->name, name_len + 1);
        n += name_len + 1;
    }
    return n;
}

/* Sized members make the next member's position known before anything is inflated. Returns the
   whole member's length, or 0 when no subfield records a size. */
static uint32_t format_extra_member_size(const uint8_t *extra, size_t len, uint32_t *data_size) {
    size_t pos = 0;

    while (pos + 4 <= len) {
        size_t sub = load_le16(extra + pos + 2);
        /* BGZF (bgzip, BAM) records the whole member's length in BC. */
        if (extra[pos] == 'B' && extra[pos + 1] == 'C' && sub == 2 && pos + 6 <= len)
            return (uint32_t)load_le16(extra + pos + 4) + 1;
        /* MiGz records only the deflate data's length in MZ, completed by the caller once the
           whole header has been measured. */
        if (extra[pos] == 'M' && extra[pos + 1] == 'Z' && sub == 4 && pos + 8 <= len)
            *data_size = load_le32(extra + pos + 4);
        pos += 4 + sub;
    }
    return 0;
}

size_t format_header_parse(const uint8_t *buf, size_t len, format_header *hdr) {
    size_t pos = FORMAT_HEADER_LEN;
    uint32_t data_size = 0;
    uint8_t flags;

    if (hdr)
        memset(hdr, 0, sizeof(*hdr));
    if (len < FORMAT_HEADER_LEN)
        return 0;
    if (!format_is_gzip(buf, len) || buf[2] != 8)
        return (size_t)-1;
    flags = buf[3];
    if (flags & 0xe0)
        return (size_t)-1;
    if (hdr)
        hdr->mtime = load_le32(buf + 4);

    if (flags & 4) { /* FEXTRA */
        size_t xlen, end;
        if (len < pos + 2)
            return 0;
        xlen = load_le16(buf + pos);
        pos += 2;
        end = pos + xlen;
        if (len < end)
            return 0;
        if (hdr)
            hdr->member_size = format_extra_member_size(buf + pos, xlen, &data_size);
        pos = end;
    }
    if (flags & 8) { /* FNAME */
        size_t start = pos;
        while (pos < len && buf[pos] != 0)
            pos++;
        if (pos >= len)
            return 0;
        if (hdr)
            hdr->name = (const char *)buf + start;
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
    /* An MZ size counts only the deflate data, the header and trailer complete the member. */
    if (hdr && hdr->member_size == 0 && data_size != 0 && data_size < UINT32_MAX - pos - 8)
        hdr->member_size = (uint32_t)pos + data_size + 8;
    return pos;
}

void format_trailer_build(uint8_t *buf, uint32_t crc, uint64_t total) {
    store_le32(buf, crc);
    store_le32(buf + 4, (uint32_t)total);
}

void format_trailer_parse(const uint8_t *buf, uint32_t *crc, uint32_t *total) {
    *crc = load_le32(buf);
    *total = load_le32(buf + 4);
}
