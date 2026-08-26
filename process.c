/* process.c -- what gzip-ng does with each file or stream
 * For conditions of distribution and use, see LICENSE.md
 */

#include "process.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "compress.h"
#include "decompress.h"

#define SUFFIX ".gz"
#define SUFFIX_LEN 3
#define MAX_PATH_LEN 4096

static void fail(const char *path) {
    fprintf(stderr, "gzip-ng: %s: %s\n", path, errno ? strerror(errno) : "processing failed");
}

/* -T writes the bytes through untouched. */
static int copy_stream(FILE *in, FILE *out) {
    char buf[1 << 16];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), in)) != 0)
        if (fwrite(buf, 1, n, out) != n)
            return -1;
    return ferror(in) ? -1 : 0;
}

static int run_stream(FILE *in, FILE *out, const gzng_options *opt) {
    if (opt->decompress)
        return gzng_decompress_stream(in, out, opt);
    if (opt->transparent)
        return copy_stream(in, out);
    return gzng_compress_stream(in, out, opt);
}

static int has_suffix(const char *path) {
    size_t n = strlen(path);
    return n > SUFFIX_LEN && strcmp(path + n - SUFFIX_LEN, SUFFIX) == 0;
}

int gzng_process_stdio(const gzng_options *opt) {
    errno = 0;
    if (run_stream(stdin, stdout, opt) != 0) {
        fail("stdin");
        return -1;
    }
    return 0;
}

/* Compression turns file into file.gz, decompression file.gz into file, or file into file
   by reading file.gz, each removing its input. */
int gzng_process_file(const char *path, const gzng_options *opt) {
    char inpath[MAX_PATH_LEN], outpath[MAX_PATH_LEN];
    FILE *in, *out;
    int rc;

    if (strlen(path) + SUFFIX_LEN >= sizeof(inpath)) {
        errno = ENAMETOOLONG;
        fail(path);
        return -1;
    }
    if (!opt->decompress) {
        snprintf(inpath, sizeof(inpath), "%s", path);
        snprintf(outpath, sizeof(outpath), "%s" SUFFIX, path);
    } else if (has_suffix(path)) {
        snprintf(inpath, sizeof(inpath), "%s", path);
        snprintf(outpath, sizeof(outpath), "%.*s", (int)(strlen(path) - SUFFIX_LEN), path);
    } else {
        snprintf(inpath, sizeof(inpath), "%s" SUFFIX, path);
        snprintf(outpath, sizeof(outpath), "%s", path);
    }
    errno = 0;
    in = fopen(inpath, "rb");
    if (in == NULL) {
        fail(inpath);
        return -1;
    }
    if (opt->stdout_mode) {
        rc = run_stream(in, stdout, opt);
        fclose(in);
        if (rc != 0 || fflush(stdout) != 0) {
            fail(inpath);
            return -1;
        }
        return 0;
    }
    out = fopen(outpath, "wb");
    if (out == NULL) {
        fail(outpath);
        fclose(in);
        return -1;
    }
    rc = run_stream(in, out, opt);
    fclose(in);
    if (fclose(out) != 0 || rc != 0) {
        fail(rc != 0 ? inpath : outpath);
        unlink(outpath);
        return -1;
    }
    if (!opt->keep)
        unlink(inpath);
    return 0;
}
