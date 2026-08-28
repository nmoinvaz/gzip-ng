/* main.c -- gzip-ng command line tool
 * For conditions of distribution and use, see LICENSE.md
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "gzfile.h"
#include "list.h"
#include "options.h"
#include "process.h"

#define MAX_PATH_LEN 4096

/* The worse of two statuses, an error outranks a warning, which outranks success. */
static int worse(int rc, int r) {
    return r == GZ_ERROR || (r == GZ_WARNING && rc == GZ_OK) ? r : rc;
}

/* One file, listed with --list, checked with --test, or processed. */
static int run_file(const char *path, const gzng_options *opt, gzng_totals *totals) {
    if (opt->list)
        return gzng_list_file(path, opt, totals);
    if (opt->test_mode)
        return gzng_test_file(path, opt);
    return gzng_process_file(path, opt);
}

/* Walk a directory for the files --recursive picks, the way gzip --recursive does. */
static int run_dir(const char *path, const gzng_options *opt, gzng_totals *totals) {
    char sub[MAX_PATH_LEN];
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
static int run_path(const char *path, const gzng_options *opt, gzng_totals *totals) {
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
    gzng_totals totals = {0, 0, 0};
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
        return gzng_process_stdio(&opt);
    }
    if (opt.list)
        gzng_list_begin(&opt);
    for (int i = 1; i <= nfiles; i++) {
        int r = strcmp(argv[i], "-") == 0 && !opt.list ? gzng_process_stdio(&opt) : run_path(argv[i], &opt, &totals);
        rc = worse(rc, r);
    }
    if (opt.list)
        gzng_list_end(&opt, &totals);
    return rc;
}
