/* gzfile.c -- gzip files by name and by header
 * For conditions of distribution and use, see LICENSE.md
 */

#include "gzfile.h"

#include <string.h>

#include "format.h"

int gzng_path_has_suffix(const char *path) {
    size_t n = strlen(path);
    return n > GZ_SUFFIX_LEN && strcmp(path + n - GZ_SUFFIX_LEN, GZ_SUFFIX) == 0;
}

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
