/* compat.h -- the POSIX pieces the command uses, provided for Windows
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_WIN32_COMPAT_H_
#define GZNG_WIN32_COMPAT_H_

#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/utime.h>

/* Symbolic links are not walked into, so a plain stat serves. */
#define lstat stat

#define fsync _commit

#ifndef S_ISDIR
#  define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#  define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

/* Just enough of dirent for the --recursive walk, over _findfirst. */
struct dirent {
    char d_name[260];
};

typedef struct {
    intptr_t handle;
    struct _finddata_t data;
    struct dirent entry;
    int32_t first;
} DIR;

static DIR *opendir(const char *path) {
    char pattern[4096];
    DIR *dir;

    if (snprintf(pattern, sizeof(pattern), "%s/*", path) >= (int32_t)sizeof(pattern))
        return NULL;
    dir = (DIR *)calloc(1, sizeof(DIR));
    if (!dir)
        return NULL;
    dir->handle = _findfirst(pattern, &dir->data);
    if (dir->handle == -1) {
        free(dir);
        return NULL;
    }
    dir->first = 1;
    return dir;
}

static struct dirent *readdir(DIR *dir) {
    if (dir->first)
        dir->first = 0;
    else if (_findnext(dir->handle, &dir->data) != 0)
        return NULL;
    snprintf(dir->entry.d_name, sizeof(dir->entry.d_name), "%s", dir->data.name);
    return &dir->entry;
}

static void closedir(DIR *dir) {
    _findclose(dir->handle);
    free(dir);
}

#endif /* GZNG_WIN32_COMPAT_H_ */
