/* main.c -- gzip-ng command line tool
 * For conditions of distribution and use, see LICENSE.md
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#  include "win32/compat.h"
#else
#  include <dirent.h>
#  include <unistd.h>
#  include <utime.h>
#endif

#include "compress.h"
#include "decompress.h"
#include "format.h"
#include "gzblock.h"
#include "options.h"

/* Gzip exit statuses. */
enum { GZ_OK = 0, GZ_ERROR = 1, GZ_WARNING = 2 };

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
    if (outname)
        fprintf(stderr, "%s:\t%5.1f%% -- replaced with %s\n", name, pct, outname);
    else
        fprintf(stderr, "%s:\t%5.1f%%\n", name, pct);
}

/* ===========================================================================
 * File names
 */

/* The suffix that names a gzip file, and the longest path the naming helpers write. */
#define GZ_SUFFIX     ".gz"
#define GZ_SUFFIX_LEN 3
#define GZ_PATH_MAX   4096

/* Whether path is named as a gzip file, by its suffix. */
static int32_t path_is_gzip(const char *path) {
    size_t n = strlen(path);
    return n > GZ_SUFFIX_LEN && strcmp(path + n - GZ_SUFFIX_LEN, GZ_SUFFIX) == 0;
}

/* Compression turns file into file.gz, decompression file.gz into file, or file into file by
   reading file.gz. Returns -1 with errno set when the name will not fit. */
static int32_t path_derive(const char *path, int32_t decompress, char *in_path, char *out_path, size_t cap) {
    if (strlen(path) + GZ_SUFFIX_LEN >= cap) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (!decompress) {
        snprintf(in_path, cap, "%s", path);
        snprintf(out_path, cap, "%s" GZ_SUFFIX, path);
    } else if (path_is_gzip(path)) {
        snprintf(in_path, cap, "%s", path);
        snprintf(out_path, cap, "%.*s", (int32_t)(strlen(path) - GZ_SUFFIX_LEN), path);
    } else {
        snprintf(in_path, cap, "%s" GZ_SUFFIX, path);
        snprintf(out_path, cap, "%s", path);
    }
    return 0;
}

/* The output name for --name, the stored name placed in the input's directory. */
static void path_from_stored(char *out_path, size_t cap, const char *in_path, const char *stored) {
    const char *slash = strrchr(in_path, '/');
    const char *base = strrchr(stored, '/');

    base = base ? base + 1 : stored;
    if (base[0] == 0)
        return;
    if (slash)
        snprintf(out_path, cap, "%.*s%s", (int32_t)(slash - in_path + 1), in_path, base);
    else
        snprintf(out_path, cap, "%s", base);
}

/* ===========================================================================
 * Input files
 */

/* Open an input read-only, with its stat. NULL with the error reported. */
static FILE *open_input(const char *path, struct stat *st) {
    FILE *in;

    errno = 0;
    in = fopen(path, "rb");
    if (in && fstat(fileno(in), st) != 0) {
        fclose(in);
        in = NULL;
    }
    if (!in)
        fail(path);
    return in;
}

/* The modification time and stored name from the header at the start of in, which is rewound
   afterwards. Returns 0 with the fields filled, name empty when absent, or -1 when not gzip. */
static int32_t read_header(FILE *in, uint32_t *mtime, char *name, size_t name_len) {
    uint8_t buf[4096];
    size_t buf_len = fread(buf, 1, sizeof(buf), in);
    format_header hdr;

    *mtime = 0;
    if (name_len != 0)
        name[0] = 0;
    if (fseek(in, 0, SEEK_SET) != 0 || !format_is_gzip(buf, buf_len))
        return -1;
    /* A header that outruns the buffer still yields the fields that fit in it. */
    if (format_header_parse(buf, buf_len, &hdr) == (size_t)-1)
        return -1;
    *mtime = hdr.mtime;
    if (hdr.name && name_len != 0)
        snprintf(name, name_len, "%s", hdr.name);
    return 0;
}

/* ===========================================================================
 * Stream processing
 */

/* -T writes the bytes through untouched. */
static int32_t copy_stream(FILE *in, FILE *out, uint64_t *count) {
    char buf[1 << 16];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), in)) != 0) {
        if (fwrite(buf, 1, n, out) != n)
            return -1;
        if (count)
            *count += n;
    }
    return ferror(in) ? -1 : 0;
}

static int32_t run_stream(FILE *in, FILE *out, const gzng_options *opt, uint32_t mtime, const char *name,
                          uint64_t *total_in, uint64_t *total_out) {
    if (opt->decompress)
        return gzng_decompress_stream(in, out, NULL, 0, opt->block_size, opt->threads, total_in, total_out);
    if (opt->transparent) {
        uint64_t n = 0;
        int32_t rc = copy_stream(in, out, &n);
        if (total_in)
            *total_in = n;
        if (total_out)
            *total_out = n;
        return rc;
    }
    /* Threads need blocks to work on, so --processes implies one. Without either this stays a
       single deflate stream. */
    uint32_t block_size = opt->block_size;
    if (block_size == 0 && opt->threads_given && opt->threads != 1)
        block_size = GZNG_DEFAULT_BLOCK;
    return gzng_compress_stream(in, out, opt->level, opt->strategy, block_size, opt->threads, opt->rsyncable, mtime,
                                name, total_in, total_out);
}

/* Under --test the first two bytes decide, since the reader passes anything but gzip through.
   They go into head for the reader to take first. Returns -1 with a warning naming the input when
   they are not gzip's. */
static int32_t test_peek(FILE *in, const char *name, const gzng_options *opt, uint8_t head[2], size_t *head_len) {
    *head_len = fread(head, 1, 2, in);
    if (format_is_gzip(head, *head_len))
        return 0;
    warn(opt, "gzip-ng: %s: not in gzip format\n", name);
    return -1;
}

/* Compressed bytes belong in a file or a pipe, gzip refuses a terminal without --force. */
static int32_t tty_guard(const gzng_options *opt) {
    if (!opt->decompress && !opt->transparent && !opt->force && isatty(fileno(stdout))) {
        fprintf(stderr, "gzip-ng: compressed data not written to a terminal, use -f to force\n");
        return GZ_ERROR;
    }
    return GZ_OK;
}

/* Filter stdin to stdout. Returns GZ_OK, or GZ_ERROR with the error reported. */
static int32_t process_stdio(const gzng_options *opt) {
    uint64_t total_in = 0, total_out = 0;
    int32_t rc;

    if (tty_guard(opt) != GZ_OK)
        return GZ_ERROR;
    errno = 0;
    if (opt->test_mode) {
        uint8_t head[2];
        size_t head_len;
        if (test_peek(stdin, "stdin", opt, head, &head_len) != 0)
            return GZ_ERROR;
        rc = gzng_decompress_stream(stdin, NULL, head, head_len, opt->block_size, opt->threads, &total_in, &total_out);
    } else {
        rc = run_stream(stdin, stdout, opt, 0, NULL, &total_in, &total_out);
    }
    if (rc != 0) {
        fail("stdin");
        return GZ_ERROR;
    }
    report(opt, "stdin", NULL, total_in, total_out);
    return GZ_OK;
}

/* ===========================================================================
 * Output file attributes
 */

/* gzip carries the input file's mode and times onto the output, and --name on decompression prefers
   the time stored in the header. */
static void copy_attrs(const char *out_path, const struct stat *ist, uint32_t mtime) {
    struct utimbuf times;

    chmod(out_path, ist->st_mode & 07777);
    times.actime = ist->st_atime;
    times.modtime = mtime != 0 ? (time_t)mtime : ist->st_mtime;
    utime(out_path, &times);
}

/* A new name is durable only once its directory is synced too. Windows has no way to ask, the
   file's own flush in process_file is all --synchronous can give there. */
#ifdef _WIN32
static void sync_dir(const char *out_path) {
    (void)out_path;
}
#else
static void sync_dir(const char *out_path) {
    const char *slash = strrchr(out_path, '/');
    char dir_path[GZ_PATH_MAX];
    int32_t fd;

    if (slash)
        snprintf(dir_path, sizeof(dir_path), "%.*s", (int32_t)(slash - out_path), out_path);
    else
        snprintf(dir_path, sizeof(dir_path), ".");
    fd = open(dir_path, O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}
#endif

/* ===========================================================================
 * Integrity testing
 */

/* Check a file's integrity without writing anything, the --test verdict. Returns GZ_OK, or
   GZ_ERROR with the error reported. */
static int32_t test_file(FILE *in, const char *path, const gzng_options *opt) {
    uint64_t total_in = 0, total_out = 0;
    uint8_t head[2];
    size_t head_len;

    if (test_peek(in, path, opt, head, &head_len) != 0)
        return GZ_ERROR;
    if (gzng_decompress_stream(in, NULL, head, head_len, opt->block_size, opt->threads, &total_in, &total_out) != 0)
        return GZ_ERROR;
    if (opt->verbose && !opt->quiet)
        fprintf(stderr, "%s:\t OK\n", path);
    return GZ_OK;
}

/* ===========================================================================
 * Listing
 */

typedef struct {
    uint64_t compressed;
    uint64_t uncompressed;
    int32_t files;
} list_totals;

/* The header row of the listing. */
static void list_begin(const gzng_options *opt) {
    if (opt->verbose)
        fprintf(stdout, "method  crc      date   time  ");
    fprintf(stdout, "  compressed uncompressed  ratio uncompressed_name\n");
}

static void list_row(const gzng_options *opt, uint32_t crc, uint32_t mtime, uint64_t compressed, uint64_t uncompressed,
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

/* List one compressed file the way gzip --list does, accumulating totals. name is the file's
   name without its suffix, shown unless --name prefers the stored one. */
static int32_t list_file(FILE *in, const char *path, const char *name, const struct stat *st, const gzng_options *opt,
                         list_totals *totals) {
    char stored[FORMAT_NAME_MAX];
    uint8_t tail[FORMAT_TRAILER_LEN];
    uint32_t mtime = 0, crc = 0;
    uint64_t uncompressed;
    size_t n;

    if (st->st_size < FORMAT_HEADER_LEN + FORMAT_TRAILER_LEN) {
        fprintf(stderr, "gzip-ng: %s: too short to be gzip\n", path);
        return GZ_ERROR;
    }
    if (read_header(in, &mtime, stored, sizeof(stored)) != 0) {
        warn(opt, "gzip-ng: %s: not in gzip format\n", path);
        return GZ_ERROR;
    }
    /* The trailer of the last member, the same 32-bit size gzip reports. */
    fseek(in, -FORMAT_TRAILER_LEN, SEEK_END);
    n = fread(tail, 1, sizeof(tail), in);
    if (n != FORMAT_TRAILER_LEN)
        return GZ_ERROR;
    {
        uint32_t size32;
        format_trailer_parse(tail, &crc, &size32);
        uncompressed = size32;
    }
    if (opt->name_mode == 1 && stored[0] != 0)
        name = stored;
    list_row(opt, crc, mtime, (uint64_t)st->st_size, uncompressed, name);
    totals->compressed += (uint64_t)st->st_size;
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
    list_row(opt, 0, 0, totals->compressed, totals->uncompressed, "(totals)");
}

/* ===========================================================================
 * Processing a file
 */

/* With --stdout the input file is left in place. */
static int32_t process_to_stdout(FILE *in, const gzng_options *opt, const char *in_path, uint32_t store_mtime,
                                 const char *store_name) {
    uint64_t total_in = 0, total_out = 0;
    int32_t rc;

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

/* Process in, named in_path, into the file named by out_path, which --name may change and
   which holds GZ_PATH_MAX. */
static int32_t process_file(FILE *in, const char *in_path, char *out_path, const struct stat *ist,
                            const gzng_options *opt) {
    uint64_t total_in = 0, total_out = 0;
    uint32_t mtime = 0;      /* to write when compressing, read when decompressing */
    const char *name = NULL; /* to write when compressing */
    FILE *out;
    int32_t rc;

    /* The name and time to record in the header, subject to --no-name, --name, --no-time, and
       --time. */
    if (!opt->decompress) {
        if (opt->name_mode != 0) {
            const char *base = strrchr(in_path, '/');
            name = base ? base + 1 : in_path;
        }
        if (opt->time_mode != 0)
            mtime = (uint32_t)ist->st_mtime;
    }
    if (opt->stdout_mode)
        return process_to_stdout(in, opt, in_path, mtime, name);
    /* With --name the stored name decides the output path, and --name or --time whether the
       stored time applies. */
    if (opt->decompress) {
        char stored[FORMAT_NAME_MAX];

        if (read_header(in, &mtime, stored, sizeof(stored)) != 0) {
            warn(opt, "gzip-ng: %s: not in gzip format\n", in_path);
            return GZ_ERROR;
        }
        if (opt->name_mode == 1 && stored[0] != 0)
            path_from_stored(out_path, GZ_PATH_MAX, in_path, stored);
        if (opt->time_mode != 1)
            mtime = 0;
    }

    out = fopen(out_path, opt->force ? "wb" : "wbx");
    if (!out) {
        if (!opt->force && errno == EEXIST) {
            warn(opt, "gzip-ng: %s already exists, not overwritten, use -f\n", out_path);
            return GZ_WARNING;
        }
        fail(out_path);
        return GZ_ERROR;
    }
    rc = run_stream(in, out, opt, mtime, name, &total_in, &total_out);
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
    copy_attrs(out_path, ist, opt->decompress ? mtime : 0);
    if (opt->synchronous)
        sync_dir(out_path);
    report(opt, in_path, out_path, total_in, total_out);
    return GZ_OK;
}

/* ===========================================================================
 * The command line
 */

/* The worse of two statuses, an error outranks a warning, which outranks success. */
static int32_t worse(int32_t rc, int32_t r) {
    return r == GZ_ERROR || (r == GZ_WARNING && rc == GZ_OK) ? r : rc;
}

/* One file, listed with --list, checked with --test, or processed. The input is opened and
   closed here, and removed once processing replaced it. */
static int32_t run_file(const char *path, const gzng_options *opt, list_totals *totals) {
    char in_path[GZ_PATH_MAX], out_path[GZ_PATH_MAX];
    struct stat st;
    FILE *in;
    int32_t rc;

    if (path_derive(path, opt->decompress || opt->list, in_path, out_path, sizeof(in_path)) != 0) {
        fail(path);
        return GZ_ERROR;
    }
    in = open_input(in_path, &st);
    if (!in)
        return GZ_ERROR;
    if (opt->list)
        rc = list_file(in, in_path, out_path, &st, opt, totals);
    else if (opt->test_mode)
        rc = test_file(in, in_path, opt);
    else
        rc = process_file(in, in_path, out_path, &st, opt);
    fclose(in);
    /* Processing replaces the input, unless --keep or --stdout leaves it in place. */
    if (rc == GZ_OK && !opt->list && !opt->test_mode && !opt->stdout_mode && !opt->keep)
        unlink(in_path);
    return rc;
}

/* Walk a directory for the files --recursive picks, the way gzip --recursive does. */
static int32_t run_dir(const char *path, const gzng_options *opt, list_totals *totals) {
    char sub[GZ_PATH_MAX];
    DIR *dir = opendir(path);
    struct dirent *e;
    /* Compression skips entries already suffixed, decompression and listing take only suffixed
       entries, the way gzip --recursive chooses files. */
    int32_t want_suffix = opt->decompress || opt->list;
    int32_t rc = GZ_OK;

    if (!dir) {
        fprintf(stderr, "gzip-ng: %s: %s\n", path, strerror(errno));
        return GZ_ERROR;
    }
    while ((e = readdir(dir))) {
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name) >= (int32_t)sizeof(sub))
            continue;
        if (lstat(sub, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            rc = worse(rc, run_dir(sub, opt, totals));
        else if (S_ISREG(st.st_mode) && path_is_gzip(sub) == want_suffix)
            rc = worse(rc, run_file(sub, opt, totals));
    }
    closedir(dir);
    return rc;
}

/* One argument, a file, or a directory walked under --recursive and a warning without. */
static int32_t run_path(const char *path, const gzng_options *opt, list_totals *totals) {
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
    int32_t nfiles = 0, rc = GZ_OK, ret;

#ifdef _WIN32
    /* Compressed bytes pass through stdio verbatim, so the console pair must not cook them. */
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
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
    for (int32_t i = 1; i <= nfiles; i++) {
        int32_t r = strcmp(argv[i], "-") == 0 && !opt.list ? process_stdio(&opt) : run_path(argv[i], &opt, &totals);
        rc = worse(rc, r);
    }
    if (opt.list)
        list_end(&opt, &totals);
    return rc;
}
