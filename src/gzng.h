#ifndef GZNG_H_
#define GZNG_H_

#define GZNG_VERSION "0.1.0"

#ifdef __cplusplus
extern "C" {
#endif

/* Version of gzip-ng. */
const char *gzng_version(void);

/* Version of the zlib-ng it was built against. */
const char *gzng_zlibng_version(void);

#ifdef __cplusplus
}
#endif

#endif
