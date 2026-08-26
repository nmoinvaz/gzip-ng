/* process.c -- what gzip-ng does with each file or stream
 * For conditions of distribution and use, see LICENSE.md
 */

#include "process.h"

#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "compress.h"
#include "gzblock.h"
#include "decompress.h"

#define SUFFIX ".gz"
#define SUFFIX_LEN 3
#define MAX_PATH_LEN 4096

static void fail(const char *path) {
    fprintf(stderr, "gzip-ng: %s: %s\n", path, errno ? strerror(errno) : "processing failed");
}

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

static int run_stream(FILE *in, FILE *out, const gzng_options *opt,
                      uint32_t mtime, const char *name,
                      uint64_t *in_len, uint64_t *out_len) {
    if (opt->decompress)
        return gzng_decompress_stream(in, out, opt, in_len, out_len);
    if (opt->transparent) {
        uint64_t n = 0;
        int rc = copy_stream(in, out, &n);
        if (in_len != NULL)
            *in_len = n;
        if (out_len != NULL)
            *out_len = n;
        return rc;
    }
    return gzng_compress_stream(in, out, opt, mtime, name, in_len, out_len);
}

/* The gzip -v report, the reduction for compression, the expansion basis for decompression. */
static void report(const gzng_options *opt, const char *name, const char *outname,
                   uint64_t in_len, uint64_t out_len) {
    uint64_t basis = opt->decompress ? out_len : in_len;
    uint64_t other = opt->decompress ? in_len : out_len;
    double pct = basis != 0 ? 100.0 * (1.0 - (double)other / (double)basis) : 0.0;

    if (!opt->verbose || opt->quiet)
        return;
    if (outname != NULL)
        fprintf(stderr, "%s:\t%5.1f%% -- replaced with %s\n", name, pct, outname);
    else
        fprintf(stderr, "%s:\t%5.1f%%\n", name, pct);
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
    uint64_t in_len = 0, out_len = 0;
    errno = 0;
    if (run_stream(stdin, opt->test_mode ? NULL : stdout, opt, 0, NULL, &in_len, &out_len) != 0) {
        fail("stdin");
        return 1;
    }
    report(opt, "stdin", NULL, in_len, out_len);
    return 0;
}

/* Compression turns file into file.gz, decompression file.gz into file, or file into file
   by reading file.gz, each removing its input. */
/* gzip carries the input file's mode and times onto the output, and -N on decompression
   prefers the time stored in the header. */
static void copy_attrs(const char *out_path, const struct stat *ist, uint32_t hdr_mtime) {
    struct timeval tv[2];

    chmod(out_path, ist->st_mode & 07777);
    tv[0].tv_sec = ist->st_atime;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = hdr_mtime != 0 ? (time_t)hdr_mtime : ist->st_mtime;
    tv[1].tv_usec = 0;
    utimes(out_path, tv);
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
    char in_path[MAX_PATH_LEN], out_path[MAX_PATH_LEN];
    FILE *in, *out;
    uint64_t in_len = 0, out_len = 0;
    uint32_t store_mtime = 0, hdr_mtime = 0;
    const char *store_name = NULL;
    struct stat ist;
    int have_ist = 0, rc;

    if (opt->test_mode) {
        uint64_t tin = 0, tout = 0;
        FILE *tf = fopen(path, "rb");
        if (tf == NULL) {
            fail(path);
            return 1;
        }
        {
            uint32_t m;
            char nm[GZBLOCK_NAME_MAX];
            if (gzng_read_meta(tf, &m, nm, sizeof(nm)) != 0) {
                if (!opt->quiet)
                    fprintf(stderr, "gzip-ng: %s: not in gzip format\n", path);
                fclose(tf);
                return 1;
            }
        }
        rc = gzng_decompress_stream(tf, NULL, opt, &tin, &tout) != 0 ? 1 : 0;
        fclose(tf);
        if (rc == 0 && opt->verbose && !opt->quiet)
            fprintf(stderr, "%s:\t OK\n", path);
        return rc;
    }
    {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (opt->recursive)
                return process_dir(path, opt);
            if (!opt->quiet)
                fprintf(stderr, "gzip-ng: %s is a directory, ignored\n", path);
            return 2;
        }
    }
    if (strlen(path) + SUFFIX_LEN >= sizeof(in_path)) {
        errno = ENAMETOOLONG;
        fail(path);
        return 1;
    }
    if (!opt->decompress) {
        snprintf(in_path, sizeof(in_path), "%s", path);
        snprintf(out_path, sizeof(out_path), "%s" SUFFIX, path);
    } else if (has_suffix(path)) {
        snprintf(in_path, sizeof(in_path), "%s", path);
        snprintf(out_path, sizeof(out_path), "%.*s", (int)(strlen(path) - SUFFIX_LEN), path);
    } else {
        snprintf(in_path, sizeof(in_path), "%s" SUFFIX, path);
        snprintf(out_path, sizeof(out_path), "%s", path);
    }
    errno = 0;
    in = fopen(in_path, "rb");
    if (in == NULL) {
        fail(in_path);
        return 1;
    }
    have_ist = fstat(fileno(in), &ist) == 0;
    if (!opt->decompress && opt->name_mode != 0 && have_ist) {
        const char *base = strrchr(in_path, '/');
        store_name = base ? base + 1 : in_path;
        store_mtime = (uint32_t)ist.st_mtime;
    }
    if (opt->decompress && !opt->stdout_mode) {
        char stored[GZBLOCK_NAME_MAX];
        if (gzng_read_meta(in, &hdr_mtime, stored, sizeof(stored)) != 0) {
            if (!opt->quiet)
                fprintf(stderr, "gzip-ng: %s: not in gzip format\n", in_path);
            fclose(in);
            return 1;
        }
        if (opt->name_mode == 1 && stored[0] != 0)
            stored_out_path(out_path, sizeof(out_path), in_path, stored);
        if (opt->name_mode != 1)
            hdr_mtime = 0;
    }
    if (opt->stdout_mode) {
        if (tty_guard(opt) != 0) {
            fclose(in);
            return 1;
        }
        rc = run_stream(in, stdout, opt, store_mtime, store_name, &in_len, &out_len);
        fclose(in);
        if (rc != 0 || fflush(stdout) != 0) {
            fail(in_path);
            return 1;
        }
        report(opt, in_path, NULL, in_len, out_len);
        return 0;
    }
    out = fopen(out_path, opt->force ? "wb" : "wbx");
    if (out == NULL) {
        if (!opt->force && errno == EEXIST) {
            if (!opt->quiet)
                fprintf(stderr, "gzip-ng: %s already exists, not overwritten, use -f\n", out_path);
            fclose(in);
            return 2;
        }
        fail(out_path);
        fclose(in);
        return 1;
    }
    rc = run_stream(in, out, opt, store_mtime, store_name, &in_len, &out_len);
    fclose(in);
    /* The output and its directory reach permanent storage before the input goes away. */
    if (rc == 0 && opt->synchronous && (fflush(out) != 0 || fsync(fileno(out)) != 0))
        rc = -1;
    if (fclose(out) != 0 || rc != 0) {
        fail(rc != 0 ? in_path : out_path);
        unlink(out_path);
        return 1;
    }
    if (have_ist)
        copy_attrs(out_path, &ist, opt->decompress ? hdr_mtime : 0);
    if (opt->synchronous) {
        const char *slash = strrchr(out_path, '/');
        char dir_path[MAX_PATH_LEN];
        int dfd;
        if (slash != NULL)
            snprintf(dir_path, sizeof(dir_path), "%.*s", (int)(slash - out_path), out_path);
        else
            snprintf(dir_path, sizeof(dir_path), ".");
        dfd = open(dir_path, O_RDONLY);
        if (dfd >= 0) {
            fsync(dfd);
            close(dfd);
        }
    }
    if (!opt->keep)
        unlink(in_path);
    report(opt, in_path, out_path, in_len, out_len);
    return 0;
}
