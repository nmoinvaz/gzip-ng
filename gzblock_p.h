/* gzblock_p.h -- private interfaces shared by the gzblock core, reader, and writer
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZBLOCK_P_H_
#define GZBLOCK_P_H_

#include "zlib-ng.h"
#include "gzblock.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IO_CHUNK      (256 * 1024)
#define MSG_LEN       128


void msg_setv(char *msg, const char *fmt, va_list ap);
void msg_set(char *msg, const char *fmt, ...);

#include "buf.h"
#include "blockdec.h"
#include "format.h"



#include "pool.h"

/* The deflate and inflate codec behind the pool, gzblock_codec() hands it over. */
int gzblock_codec_init(pool_t *p, zng_stream *z);
void gzblock_codec_end(pool_t *p, zng_stream *z);
void gzblock_codec_run(pool_t *p, zng_stream *z, slot_t *slot);

#endif /* GZBLOCK_P_H_ */
