/* process.c -- what gzip-ng does with each file or stream
 * For conditions of distribution and use, see LICENSE.md
 */

#include "process.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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

/* Compressed bytes belong in a file or a pipe, gzip refuses a terminal without -f. */
static int tty_guard(const gzng_options *opt) {
    if (!opt->decompress && !opt->transparent && !opt->force && isatty(fileno(stdout))) {
        fprintf(stderr, "gzip-ng: compressed data not written to a terminal, use -f to force\n");
        return 1;
    }
    return 0;
}

int gzng_process_stdio(const gzng_options *opt) {
    if (tty_guard(opt) != 0)
        return 1;
    errno = 0;
    if (run_stream(stdin, stdout, opt) != 0) {
        fail("stdin");
        return 1;
    }
    return 0;
}

/* Compression turns file into file.gz, decompression file.gz into file, or file into file
   by reading file.gz, each removing its input. */
/* Walk a directory. Compression skips entries already suffixed, decompression takes only
   suffixed entries, the way gzip -r chooses files. */
static int process_dir(const char *path, const gzng_options *opt) {
    char sub[MAX_PATH_LEN];
    DIR *dir = opendir(path);
    struct dirent *e;
    int rc = 0, r = 0;

    if (dir == NULL) {
        fail(path);
        return 1;
    }
    while ((e = readdir(dir)) != NULL) {
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name) >= (int)sizeof(sub))
            continue;
        if (lstat(sub, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            r = process_dir(sub, opt);
        else if (!S_ISREG(st.st_mode))
            continue;
        else if (opt->decompress ? !has_suffix(sub) : has_suffix(sub))
            continue;
        else
            r = gzng_process_file(sub, opt);
        if (r == 1 || (r == 2 && rc == 0))
            rc = r;
    }
    closedir(dir);
    return rc;
}

int gzng_process_file(const char *path, const gzng_options *opt) {
    char inpath[MAX_PATH_LEN], outpath[MAX_PATH_LEN];
    FILE *in, *out;
    int rc;

    {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (opt->recursive)
                return process_dir(path, opt);
            fprintf(stderr, "gzip-ng: %s is a directory, ignored\n", path);
            return 2;
        }
    }
    if (strlen(path) + SUFFIX_LEN >= sizeof(inpath)) {
        errno = ENAMETOOLONG;
        fail(path);
        return 1;
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
        if (tty_guard(opt) != 0) {
            fclose(in);
            return 1;
        }
        rc = run_stream(in, stdout, opt);
        fclose(in);
        if (rc != 0 || fflush(stdout) != 0) {
            fail(inpath);
            return 1;
        }
        return 0;
    }
    out = fopen(outpath, opt->force ? "wb" : "wbx");
    if (out == NULL) {
        if (!opt->force && errno == EEXIST) {
            fprintf(stderr, "gzip-ng: %s already exists, not overwritten, use -f\n", outpath);
            fclose(in);
            return 2;
        }
        fail(outpath);
        fclose(in);
        return 1;
    }
    rc = run_stream(in, out, opt);
    fclose(in);
    if (fclose(out) != 0 || rc != 0) {
        fail(rc != 0 ? inpath : outpath);
        unlink(outpath);
        return 1;
    }
    if (!opt->keep)
        unlink(inpath);
    return 0;
}
