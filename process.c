/* process.c -- what gzip-ng does with each file or stream
 * For conditions of distribution and use, see LICENSE.md
 */

#include "process.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "compress.h"
#include "decompress.h"
#include "gzblock.h"

#define SUFFIX       ".gz"
#define SUFFIX_LEN   3
#define MAX_PATH_LEN 4096

/* ===========================================================================
 * Diagnostics
 */

static void fail(const char *path) {
    fprintf(stderr, "gzip-ng: %s: %s\n", path, errno ? strerror(errno) : "processing failed");
}

static void warn(const gzng_options *opt, const char *fmt, const char *arg) {
    if (!opt->quiet)
        fprintf(stderr, fmt, arg);
}

/* The gzip -v report, the reduction for compression, the expansion basis for decompression. */
static void report(const gzng_options *opt, const char *name, const char *outname, uint64_t total_in,
                   uint64_t total_out) {
    uint64_t basis = opt->decompress ? total_out : total_in;
    uint64_t other = opt->decompress ? total_in : total_out;
    double pct = basis != 0 ? 100.0 * (1.0 - (double)other / (double)basis) : 0.0;

    if (!opt->verbose || opt->quiet)
        return;
    if (outname != NULL)
        fprintf(stderr, "%s:\t%5.1f%% -- replaced with %s\n", name, pct, outname);
    else
        fprintf(stderr, "%s:\t%5.1f%%\n", name, pct);
}

/* ===========================================================================
 * Stream processing
 */

/* -T writes the bytes through untouched. */
static int copy_stream(FILE *in, FILE *out, uint64_t *count) {
    char buf[1 << 16];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), in)) != 0) {
        if (fwrite(buf, 1, n, out) != n)
            return -1;
        if (count != NULL)
            *count += n;
    }
    return ferror(in) ? -1 : 0;
}

static int run_stream(FILE *in, FILE *out, const gzng_options *opt, uint32_t mtime, const char *name,
                      uint64_t *total_in, uint64_t *total_out) {
    if (opt->decompress)
        return gzng_decompress_stream(in, out, opt, total_in, total_out);
    if (opt->transparent) {
        uint64_t n = 0;
        int rc = copy_stream(in, out, &n);
        if (total_in != NULL)
            *total_in = n;
        if (total_out != NULL)
            *total_out = n;
        return rc;
    }
    return gzng_compress_stream(in, out, opt, mtime, name, total_in, total_out);
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
    uint64_t total_in = 0, total_out = 0;

    if (tty_guard(opt) != 0)
        return 1;
    errno = 0;
    if (run_stream(stdin, opt->test_mode ? NULL : stdout, opt, 0, NULL, &total_in, &total_out) != 0) {
        fail("stdin");
        return 1;
    }
    report(opt, "stdin", NULL, total_in, total_out);
    return 0;
}

/* ===========================================================================
 * Input and output paths
 */

int gzng_path_has_suffix(const char *path) {
    size_t n = strlen(path);
    return n > SUFFIX_LEN && strcmp(path + n - SUFFIX_LEN, SUFFIX) == 0;
}

/* Compression turns file into file.gz, decompression file.gz into file, or file into file by
   reading file.gz. Returns -1 with errno set when the name will not fit. */
static int derive_paths(const char *path, const gzng_options *opt, char *in_path, char *out_path, size_t cap) {
    if (strlen(path) + SUFFIX_LEN >= cap) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (!opt->decompress) {
        snprintf(in_path, cap, "%s", path);
        snprintf(out_path, cap, "%s" SUFFIX, path);
    } else if (gzng_path_has_suffix(path)) {
        snprintf(in_path, cap, "%s", path);
        snprintf(out_path, cap, "%.*s", (int)(strlen(path) - SUFFIX_LEN), path);
    } else {
        snprintf(in_path, cap, "%s" SUFFIX, path);
        snprintf(out_path, cap, "%s", path);
    }
    return 0;
}

/* The output name for -N, the stored name placed in the input's directory. */
static void stored_out_path(char *out_path, size_t cap, const char *in_path, const char *stored) {
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

/* ===========================================================================
 * Header fields and file attributes
 */

/* The name and time to record in the header, subject to -n, -N, -m, and -M. */
static void store_meta(const gzng_options *opt, const char *in_path, const struct stat *ist, uint32_t *mtime,
                       const char **name) {
    if (opt->name_mode != 0) {
        const char *base = strrchr(in_path, '/');
        *name = base ? base + 1 : in_path;
    }
    if (opt->time_mode != 0)
        *mtime = (uint32_t)ist->st_mtime;
}

/* Read the stored name and time. With -N the stored name decides the output path.
   Returns -1 when the input is not gzip. */
static int restore_meta(FILE *in, const gzng_options *opt, const char *in_path, char *out_path, size_t cap,
                        uint32_t *hdr_mtime) {
    char stored[GZBLOCK_NAME_MAX];

    if (gzng_read_meta(in, hdr_mtime, stored, sizeof(stored)) != 0) {
        warn(opt, "gzip-ng: %s: not in gzip format\n", in_path);
        return -1;
    }
    if (opt->name_mode == 1 && stored[0] != 0)
        stored_out_path(out_path, cap, in_path, stored);
    if (opt->time_mode != 1)
        *hdr_mtime = 0;
    return 0;
}

/* gzip carries the input file's mode and times onto the output, and -N on decompression prefers
   the time stored in the header. */
static void copy_attrs(const char *out_path, const struct stat *ist, uint32_t hdr_mtime) {
    struct timeval tv[2];

    chmod(out_path, ist->st_mode & 07777);
    tv[0].tv_sec = ist->st_atime;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = hdr_mtime != 0 ? (time_t)hdr_mtime : ist->st_mtime;
    tv[1].tv_usec = 0;
    utimes(out_path, tv);
}

/* A new name is durable only once its directory is synced too. */
static void sync_dir(const char *out_path) {
    const char *slash = strrchr(out_path, '/');
    char dir_path[MAX_PATH_LEN];
    int fd;

    if (slash != NULL)
        snprintf(dir_path, sizeof(dir_path), "%.*s", (int)(slash - out_path), out_path);
    else
        snprintf(dir_path, sizeof(dir_path), ".");
    fd = open(dir_path, O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}

/* ===========================================================================
 * Integrity testing
 */

static int test_file(const char *path, const gzng_options *opt) {
    uint64_t total_in = 0, total_out = 0;
    char stored[GZBLOCK_NAME_MAX];
    uint32_t mtime;
    FILE *in;
    int rc;

    errno = 0;
    in = fopen(path, "rb");
    if (in == NULL) {
        fail(path);
        return 1;
    }
    /* Non-gzip input fails the test rather than passing through. */
    if (gzng_read_meta(in, &mtime, stored, sizeof(stored)) != 0) {
        warn(opt, "gzip-ng: %s: not in gzip format\n", path);
        fclose(in);
        return 1;
    }
    rc = gzng_decompress_stream(in, NULL, opt, &total_in, &total_out) != 0 ? 1 : 0;
    fclose(in);
    if (rc == 0 && opt->verbose && !opt->quiet)
        fprintf(stderr, "%s:\t OK\n", path);
    return rc;
}

/* ===========================================================================
 * Processing a file
 */

/* With -c the input file is left in place. */
static int process_to_stdout(FILE *in, const gzng_options *opt, const char *in_path, uint32_t store_mtime,
                             const char *store_name) {
    uint64_t total_in = 0, total_out = 0;
    int rc;

    if (tty_guard(opt) != 0)
        return 1;
    rc = run_stream(in, stdout, opt, store_mtime, store_name, &total_in, &total_out);
    if (rc != 0 || fflush(stdout) != 0) {
        fail(in_path);
        return 1;
    }
    report(opt, in_path, NULL, total_in, total_out);
    return 0;
}

int gzng_process_file(const char *path, const gzng_options *opt) {
    char in_path[MAX_PATH_LEN], out_path[MAX_PATH_LEN];
    uint64_t total_in = 0, total_out = 0;
    uint32_t store_mtime = 0, hdr_mtime = 0;
    const char *store_name = NULL;
    struct stat ist;
    FILE *in, *out;
    int have_ist, rc;

    if (opt->test_mode)
        return test_file(path, opt);
    if (derive_paths(path, opt, in_path, out_path, sizeof(in_path)) != 0) {
        fail(path);
        return 1;
    }

    errno = 0;
    in = fopen(in_path, "rb");
    if (in == NULL) {
        fail(in_path);
        return 1;
    }
    have_ist = fstat(fileno(in), &ist) == 0;
    if (!opt->decompress && have_ist)
        store_meta(opt, in_path, &ist, &store_mtime, &store_name);
    if (opt->decompress && !opt->stdout_mode &&
        restore_meta(in, opt, in_path, out_path, sizeof(out_path), &hdr_mtime) != 0) {
        fclose(in);
        return 1;
    }
    if (opt->stdout_mode) {
        rc = process_to_stdout(in, opt, in_path, store_mtime, store_name);
        fclose(in);
        return rc;
    }

    out = fopen(out_path, opt->force ? "wb" : "wbx");
    if (out == NULL) {
        if (!opt->force && errno == EEXIST) {
            warn(opt, "gzip-ng: %s already exists, not overwritten, use -f\n", out_path);
            fclose(in);
            return 2;
        }
        fail(out_path);
        fclose(in);
        return 1;
    }
    rc = run_stream(in, out, opt, store_mtime, store_name, &total_in, &total_out);
    fclose(in);
    /* --synchronous flushes the output before the input is unlinked. */
    if (rc == 0 && opt->synchronous && (fflush(out) != 0 || fsync(fileno(out)) != 0))
        rc = 1;
    if (fclose(out) != 0 || rc != 0) {
        fail(rc != 0 ? in_path : out_path);
        unlink(out_path);
        return 1;
    }
    if (have_ist)
        copy_attrs(out_path, &ist, opt->decompress ? hdr_mtime : 0);
    if (opt->synchronous)
        sync_dir(out_path);
    if (!opt->keep)
        unlink(in_path);
    report(opt, in_path, out_path, total_in, total_out);
    return 0;
}
