/* gzblock_p.h -- private interfaces shared by the gzblock core, reader, and writer
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZBLOCK_P_H_
#define GZBLOCK_P_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockdec.h"
#include "buf.h"
#include "format.h"
#include "gzblock.h"
#include "pool.h"
#include "rolling.h"
#include "util.h"
#include "zlib-ng.h"

#define IO_CHUNK (256 * 1024) /* read and write in this much at a time */
#define MSG_LEN  128          /* room for one error message */

typedef struct {
    pool_t pool;
    size_t next_produce, next_emit;
    int pool_up;
} pipeline_t;

/* The deflate and inflate codec, which the reader and writer hand to their pool. */
int gzblock_codec_init(pool_t *pool, zng_stream *z);
void gzblock_codec_end(pool_t *pool, zng_stream *z);
void gzblock_codec_run(pool_t *pool, zng_stream *z, slot_t *slot);

#endif /* GZBLOCK_P_H_ */
