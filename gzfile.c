/* gzfile.c -- the names of gzip files
 * For conditions of distribution and use, see LICENSE.md
 */

#include "gzfile.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

int gzng_path_has_suffix(const char *path) {
    size_t n = strlen(path);
    return n > GZ_SUFFIX_LEN && strcmp(path + n - GZ_SUFFIX_LEN, GZ_SUFFIX) == 0;
}

/* Compression turns file into file.gz, decompression file.gz into file, or file into file by
   reading file.gz. Returns -1 with errno set when the name will not fit. */
int gzng_path_derive(const char *path, int decompress, char *in_path, char *out_path, size_t cap) {
    if (strlen(path) + GZ_SUFFIX_LEN >= cap) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (!decompress) {
        snprintf(in_path, cap, "%s", path);
        snprintf(out_path, cap, "%s" GZ_SUFFIX, path);
    } else if (gzng_path_has_suffix(path)) {
        snprintf(in_path, cap, "%s", path);
        snprintf(out_path, cap, "%.*s", (int)(strlen(path) - GZ_SUFFIX_LEN), path);
    } else {
        snprintf(in_path, cap, "%s" GZ_SUFFIX, path);
        snprintf(out_path, cap, "%s", path);
    }
    return 0;
}

/* The output name for --name, the stored name placed in the input's directory. */
void gzng_path_from_stored(char *out_path, size_t cap, const char *in_path, const char *stored) {
    const char *slash = strrchr(in_path, '/');
    const char *base = strrchr(stored, '/');

    base = base ? base + 1 : stored;
    if (base[0] == 0)
        return;
    if (slash != NULL)
        snprintf(out_path, cap, "%.*s%s", (int)(slash - in_path + 1), in_path, base);
    else
        snprintf(out_path, cap, "%s", base);
}
