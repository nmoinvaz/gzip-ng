/* main.c -- gzip-ng command line tool
 * For conditions of distribution and use, see LICENSE.md
 */

#include <dirent.h>
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
#include "format.h"
#include "gzblock.h"
#include "gzfile.h"
#include "options.h"

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

/* The gzip --verbose report, the reduction for compression, the expansion basis for
   decompression. */
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
 * Input files
 */

/* Open an input read-only, with its stat when st is not NULL. NULL with the error reported. */
static FILE *open_input(const char *path, struct stat *st) {
    FILE *in;

    errno = 0;
    in = fopen(path, "rb");
    if (in != NULL && st != NULL && fstat(fileno(in), st) != 0) {
        fclose(in);
        in = NULL;
    }
    if (in == NULL)
        fail(path);
    return in;
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

/* Compressed bytes belong in a file or a pipe, gzip refuses a terminal without --force. */
static int tty_guard(const gzng_options *opt) {
    if (!opt->decompress && !opt->transparent && !opt->force && isatty(fileno(stdout))) {
        fprintf(stderr, "gzip-ng: compressed data not written to a terminal, use -f to force\n");
        return GZ_ERROR;
    }
    return GZ_OK;
}

/* Filter stdin to stdout. Returns GZ_OK, or GZ_ERROR with the error reported. */
static int process_stdio(const gzng_options *opt) {
    uint64_t total_in = 0, total_out = 0;

    if (tty_guard(opt) != GZ_OK)
        return GZ_ERROR;
    errno = 0;
    if (run_stream(stdin, opt->test_mode ? NULL : stdout, opt, 0, NULL, &total_in, &total_out) != 0) {
        fail("stdin");
        return GZ_ERROR;
    }
    report(opt, "stdin", NULL, total_in, total_out);
    return GZ_OK;
}

/* ===========================================================================
 * Header fields and file attributes
 */

/* The name and time to record in the header, subject to --no-name, --name, --no-time, and
   --time. */
static void store_meta(const gzng_options *opt, const char *in_path, const struct stat *ist, uint32_t *mtime,
                       const char **name) {
    if (opt->name_mode != 0) {
        const char *base = strrchr(in_path, '/');
        *name = base ? base + 1 : in_path;
    }
    if (opt->time_mode != 0)
        *mtime = (uint32_t)ist->st_mtime;
}

/* Read the stored name and time. With --name the stored name decides the output path.
   Returns -1 when the input is not gzip. */
static int restore_meta(FILE *in, const gzng_options *opt, const char *in_path, char *out_path, size_t cap,
                        uint32_t *hdr_mtime) {
    char stored[FORMAT_NAME_MAX];

    if (gzng_read_meta(in, hdr_mtime, stored, sizeof(stored)) != 0) {
        warn(opt, "gzip-ng: %s: not in gzip format\n", in_path);
        return -1;
    }
    if (opt->name_mode == 1 && stored[0] != 0)
        gzng_path_from_stored(out_path, cap, in_path, stored);
    if (opt->time_mode != 1)
        *hdr_mtime = 0;
    return 0;
}

/* gzip carries the input file's mode and times onto the output, and --name on decompression prefers
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
    char dir_path[GZ_PATH_MAX];
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

/* Check one file's integrity without writing anything, the --test verdict. Returns GZ_OK, or
   GZ_ERROR with the error reported. */
static int test_file(const char *path, const gzng_options *opt) {
    uint64_t total_in = 0, total_out = 0;
    char stored[FORMAT_NAME_MAX];
    uint32_t mtime;
    FILE *in;
    int rc;

    in = open_input(path, NULL);
    if (in == NULL)
        return GZ_ERROR;
    /* Non-gzip input fails the test rather than passing through. */
    if (gzng_read_meta(in, &mtime, stored, sizeof(stored)) != 0) {
        warn(opt, "gzip-ng: %s: not in gzip format\n", path);
        fclose(in);
        return GZ_ERROR;
    }
    rc = gzng_decompress_stream(in, NULL, opt, &total_in, &total_out) != 0 ? GZ_ERROR : GZ_OK;
    fclose(in);
    if (rc == GZ_OK && opt->verbose && !opt->quiet)
        fprintf(stderr, "%s:\t OK\n", path);
    return rc;
}

/* ===========================================================================
 * Listing
 */

typedef struct {
    uint64_t compressed;
    uint64_t uncompressed;
    int files;
} list_totals;

/* The header row of the listing. */
static void list_begin(const gzng_options *opt) {
    if (opt->verbose)
        fprintf(stdout, "method  crc      date   time  ");
    fprintf(stdout, "  compressed uncompressed  ratio uncompressed_name\n");
}

static void row(const gzng_options *opt, uint32_t crc, uint32_t mtime, uint64_t compressed, uint64_t uncompressed,
                const char *name) {
    double pct = uncompressed != 0 ? 100.0 * (1.0 - (double)compressed / (double)uncompressed) : 0.0;

    if (opt->verbose) {
        char when[24] = "";
        time_t t = (time_t)mtime;
        if (mtime != 0)
            strftime(when, sizeof(when), "%b %e %H:%M", localtime(&t));
        fprintf(stdout, "defla %08x %-12s  ", crc, when);
    }
    fprintf(stdout, "%12llu %12llu %5.1f%% %s\n", (unsigned long long)compressed, (unsigned long long)uncompressed, pct,
            name);
}

/* List one compressed file the way gzip --list does, accumulating totals. */
static int list_file(const char *path, const gzng_options *opt, list_totals *totals) {
    char stored[FORMAT_NAME_MAX], name_buf[4096];
    const char *name = path;
    uint8_t tail[FORMAT_TRAILER_LEN];
    uint32_t mtime = 0, crc = 0;
    uint64_t uncompressed;
    struct stat st;
    size_t n;
    FILE *in;

    in = open_input(path, &st);
    if (in == NULL)
        return GZ_ERROR;
    if (st.st_size < FORMAT_HEADER_LEN + FORMAT_TRAILER_LEN) {
        fprintf(stderr, "gzip-ng: %s: too short to be gzip\n", path);
        fclose(in);
        return GZ_ERROR;
    }
    if (gzng_read_meta(in, &mtime, stored, sizeof(stored)) != 0) {
        fprintf(stderr, "gzip-ng: %s: not in gzip format\n", path);
        fclose(in);
        return GZ_ERROR;
    }
    /* The trailer of the last member, the same 32-bit size gzip reports. */
    fseek(in, -FORMAT_TRAILER_LEN, SEEK_END);
    n = fread(tail, 1, sizeof(tail), in);
    fclose(in);
    if (n != FORMAT_TRAILER_LEN)
        return GZ_ERROR;
    {
        uint32_t size32;
        format_trailer_parse(tail, &crc, &size32);
        uncompressed = size32;
    }

    if (opt->name_mode == 1 && stored[0] != 0) {
        name = stored;
    } else {
        size_t len = strlen(path);
        if (gzng_path_has_suffix(path) && len - GZ_SUFFIX_LEN < sizeof(name_buf)) {
            memcpy(name_buf, path, len - GZ_SUFFIX_LEN);
            name_buf[len - GZ_SUFFIX_LEN] = 0;
            name = name_buf;
        }
    }
    row(opt, crc, mtime, (uint64_t)st.st_size, uncompressed, name);
    totals->compressed += (uint64_t)st.st_size;
    totals->uncompressed += uncompressed;
    totals->files++;
    return GZ_OK;
}

/* The totals row, once more than one file was listed. */
static void list_end(const gzng_options *opt, const list_totals *totals) {
    if (totals->files < 2)
        return;
    if (opt->verbose)
        fprintf(stdout, "                              ");
    row(opt, 0, 0, totals->compressed, totals->uncompressed, "(totals)");
}

/* ===========================================================================
 * Processing a file
 */

/* With --stdout the input file is left in place. */
static int process_to_stdout(FILE *in, const gzng_options *opt, const char *in_path, uint32_t store_mtime,
                             const char *store_name) {
    uint64_t total_in = 0, total_out = 0;
    int rc;

    if (tty_guard(opt) != GZ_OK)
        return GZ_ERROR;
    rc = run_stream(in, stdout, opt, store_mtime, store_name, &total_in, &total_out);
    if (rc != 0 || fflush(stdout) != 0) {
        fail(in_path);
        return GZ_ERROR;
    }
    report(opt, in_path, NULL, total_in, total_out);
    return GZ_OK;
}

/* Process one file, never a directory. Returns GZ_OK, GZ_ERROR, or GZ_WARNING, the last two
   reported. */
static int process_file(const char *path, const gzng_options *opt) {
    char in_path[GZ_PATH_MAX], out_path[GZ_PATH_MAX];
    uint64_t total_in = 0, total_out = 0;
    uint32_t mtime = 0;      /* to write when compressing, read when decompressing */
    const char *name = NULL; /* to write when compressing */
    struct stat ist;
    FILE *in, *out;
    int rc;

    if (gzng_path_derive(path, opt->decompress, in_path, out_path, sizeof(in_path)) != 0) {
        fail(path);
        return GZ_ERROR;
    }

    in = open_input(in_path, &ist);
    if (in == NULL)
        return GZ_ERROR;
    if (!opt->decompress)
        store_meta(opt, in_path, &ist, &mtime, &name);
    if (opt->stdout_mode) {
        rc = process_to_stdout(in, opt, in_path, mtime, name);
        fclose(in);
        return rc;
    }
    if (opt->decompress && restore_meta(in, opt, in_path, out_path, sizeof(out_path), &mtime) != 0) {
        fclose(in);
        return GZ_ERROR;
    }

    out = fopen(out_path, opt->force ? "wb" : "wbx");
    if (out == NULL) {
        if (!opt->force && errno == EEXIST) {
            warn(opt, "gzip-ng: %s already exists, not overwritten, use -f\n", out_path);
            fclose(in);
            return GZ_WARNING;
        }
        fail(out_path);
        fclose(in);
        return GZ_ERROR;
    }
    rc = run_stream(in, out, opt, mtime, name, &total_in, &total_out);
    fclose(in);
    /* --synchronous flushes the output before the input is unlinked. */
    if (rc == 0 && opt->synchronous) {
        if (fflush(out) != 0 || fsync(fileno(out)) != 0)
            rc = -1;
    }
    if (fclose(out) != 0 || rc != 0) {
        fail(rc != 0 ? in_path : out_path);
        unlink(out_path);
        return GZ_ERROR;
    }
    copy_attrs(out_path, &ist, opt->decompress ? mtime : 0);
    if (opt->synchronous)
        sync_dir(out_path);
    if (!opt->keep)
        unlink(in_path);
    report(opt, in_path, out_path, total_in, total_out);
    return GZ_OK;
}

/* ===========================================================================
 * The command line
 */

/* The worse of two statuses, an error outranks a warning, which outranks success. */
static int worse(int rc, int r) {
    return r == GZ_ERROR || (r == GZ_WARNING && rc == GZ_OK) ? r : rc;
}

/* One file, listed with --list, checked with --test, or processed. */
static int run_file(const char *path, const gzng_options *opt, list_totals *totals) {
    if (opt->list)
        return list_file(path, opt, totals);
    if (opt->test_mode)
        return test_file(path, opt);
    return process_file(path, opt);
}

/* Walk a directory for the files --recursive picks, the way gzip --recursive does. */
static int run_dir(const char *path, const gzng_options *opt, list_totals *totals) {
    char sub[GZ_PATH_MAX];
    DIR *dir = opendir(path);
    struct dirent *e;
    /* Compression skips entries already suffixed, decompression and listing take only suffixed
       entries, the way gzip --recursive chooses files. */
    int want_suffix = opt->decompress || opt->list;
    int rc = GZ_OK;

    if (dir == NULL) {
        fprintf(stderr, "gzip-ng: %s: %s\n", path, strerror(errno));
        return GZ_ERROR;
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
            rc = worse(rc, run_dir(sub, opt, totals));
        else if (S_ISREG(st.st_mode) && gzng_path_has_suffix(sub) == want_suffix)
            rc = worse(rc, run_file(sub, opt, totals));
    }
    closedir(dir);
    return rc;
}

/* One argument, a file, or a directory walked under --recursive and a warning without. */
static int run_path(const char *path, const gzng_options *opt, list_totals *totals) {
    struct stat st;

    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (opt->recursive)
            return run_dir(path, opt, totals);
        if (!opt->quiet)
            fprintf(stderr, "gzip-ng: %s is a directory, ignored\n", path);
        return GZ_WARNING;
    }
    return run_file(path, opt, totals);
}

int main(int argc, char **argv) {
    gzng_options opt;
    list_totals totals = {0, 0, 0};
    int nfiles = 0, rc = GZ_OK, ret;

    gzng_options_init(&opt);
    gzng_options_personas(&opt, argv[0]);
    ret = gzng_options_parse(&opt, argc, argv, &nfiles);
    if (ret == 1)
        return GZ_OK;
    if (ret < 0)
        return GZ_ERROR;
    if (nfiles == 0) {
        if (opt.list) {
            fprintf(stderr, "gzip-ng: -l needs file arguments\n");
            return GZ_ERROR;
        }
        return process_stdio(&opt);
    }
    if (opt.list)
        list_begin(&opt);
    for (int i = 1; i <= nfiles; i++) {
        int r = strcmp(argv[i], "-") == 0 && !opt.list ? process_stdio(&opt) : run_path(argv[i], &opt, &totals);
        rc = worse(rc, r);
    }
    if (opt.list)
        list_end(&opt, &totals);
    return rc;
}
