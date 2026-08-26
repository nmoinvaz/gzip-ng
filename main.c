/* main.c -- gzip-ng command line tool
 * For conditions of distribution and use, see LICENSE.md
 */

#include <string.h>

#include "options.h"
#include "process.h"

int main(int argc, char **argv) {
    gzng_options opt;
    int nfiles = 0, rc = 0, ret;

    gzng_options_init(&opt);
    gzng_options_personas(&opt, argv[0]);
    ret = gzng_options_parse(&opt, argc, argv, &nfiles);
    if (ret == 1)
        return 0;
    if (ret < 0)
        return 1;
    if (nfiles == 0)
        return gzng_process_stdio(&opt);
    for (int i = 1; i <= nfiles; i++) {
        int r = strcmp(argv[i], "-") == 0 ? gzng_process_stdio(&opt)
                                          : gzng_process_file(argv[i], &opt);
        /* gzip's convention, 1 for errors beats 2 for warnings beats 0 */
        if (r == 1 || (r == 2 && rc == 0))
            rc = r;
    }
    return rc;
}
