/* list.c -- the gzip -l listing
 * For conditions of distribution and use, see LICENSE.md
 */

#include "list.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "decompress.h"
#include "gzblock.h"

void gzng_list_begin(const gzng_options *opt) {
    if (opt->verbose)
        fprintf(stdout, "method  crc      date   time  ");
    fprintf(stdout, "  compressed uncompressed  ratio uncompressed_name\n");
}

static void row(const gzng_options *opt, uint32_t crc, uint32_t mtime,
                uint64_t compressed, uint64_t uncompressed, const char *name) {
    double pct = uncompressed != 0 ? 100.0 * (1.0 - (double)compressed / (double)uncompressed)
                                   : 0.0;

    if (opt->verbose) {
        char when[24] = "";
        time_t t = (time_t)mtime;
        if (mtime != 0)
            strftime(when, sizeof(when), "%b %e %H:%M", localtime(&t));
        fprintf(stdout, "defla %08x %-12s  ", crc, when);
    }
    fprintf(stdout, "%12llu %12llu %5.1f%% %s\n",
            (unsigned long long)compressed, (unsigned long long)uncompressed, pct, name);
}

int gzng_list_file(const char *path, const gzng_options *opt, gzng_totals *totals) {
    char stored[GZBLOCK_NAME_MAX], namebuf[4096];
    const char *name = path;
    uint8_t tail[8];
    uint32_t mtime = 0, crc = 0;
    uint64_t uncompressed;
    struct stat st;
    size_t n;
    FILE *in;

    errno = 0;
    in = fopen(path, "rb");
    if (in == NULL || fstat(fileno(in), &st) != 0 || st.st_size < 18) {
        fprintf(stderr, "gzip-ng: %s: %s\n", path,
                errno ? strerror(errno) : "too short to be gzip");
        if (in != NULL)
            fclose(in);
        return 1;
    }
    if (gzng_read_meta(in, &mtime, stored, sizeof(stored)) != 0) {
        fprintf(stderr, "gzip-ng: %s: not in gzip format\n", path);
        fclose(in);
        return 1;
    }
    /* The trailer of the last member, the same 32-bit size gzip reports. */
    fseek(in, -8, SEEK_END);
    n = fread(tail, 1, 8, in);
    fclose(in);
    if (n != 8)
        return 1;
    crc = (uint32_t)tail[0] | ((uint32_t)tail[1] << 8) | ((uint32_t)tail[2] << 16) |
          ((uint32_t)tail[3] << 24);
    uncompressed = (uint64_t)tail[4] | ((uint64_t)tail[5] << 8) | ((uint64_t)tail[6] << 16) |
                   ((uint64_t)tail[7] << 24);

    if (opt->name_mode == 1 && stored[0] != 0) {
        name = stored;
    } else {
        size_t len = strlen(path);
        if (len > 3 && strcmp(path + len - 3, ".gz") == 0 && len - 3 < sizeof(namebuf)) {
            memcpy(namebuf, path, len - 3);
            namebuf[len - 3] = 0;
            name = namebuf;
        }
    }
    row(opt, crc, mtime, (uint64_t)st.st_size, uncompressed, name);
    totals->compressed += (uint64_t)st.st_size;
    totals->uncompressed += uncompressed;
    totals->files++;
    return 0;
}

void gzng_list_end(const gzng_options *opt, const gzng_totals *totals) {
    if (totals->files < 2)
        return;
    if (opt->verbose)
        fprintf(stdout, "                              ");
    row(opt, 0, 0, totals->compressed, totals->uncompressed, "(totals)");
}
