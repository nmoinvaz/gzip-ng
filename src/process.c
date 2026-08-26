/* process.c -- what gzip-ng does with each file or stream
 * For conditions of distribution and use, see LICENSE.md
 */

#include "process.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "compress.h"

#define SUFFIX ".gz"
#define MAX_PATH_LEN 4096

static void fail(const char *path) {
    fprintf(stderr, "gzip-ng: %s: %s\n", path, errno ? strerror(errno) : "processing failed");
}

int gzng_process_stdio(const gzng_options *opt) {
    errno = 0;
    if (gzng_compress_stream(stdin, stdout, opt) != 0) {
        fail("stdin");
        return -1;
    }
    return 0;
}

int gzng_process_file(const char *path, const gzng_options *opt) {
    char outpath[MAX_PATH_LEN];
    FILE *in, *out;
    int rc;

    if (snprintf(outpath, sizeof(outpath), "%s" SUFFIX, path) >= (int)sizeof(outpath)) {
        errno = ENAMETOOLONG;
        fail(path);
        return -1;
    }
    errno = 0;
    in = fopen(path, "rb");
    if (in == NULL) {
        fail(path);
        return -1;
    }
    out = fopen(outpath, "wb");
    if (out == NULL) {
        fail(outpath);
        fclose(in);
        return -1;
    }
    rc = gzng_compress_stream(in, out, opt);
    fclose(in);
    if (fclose(out) != 0 || rc != 0) {
        fail(rc != 0 ? path : outpath);
        unlink(outpath);
        return -1;
    }
    if (!opt->keep)
        unlink(path);
    return 0;
}
