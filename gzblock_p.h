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
#define GZ_TRAILER    8
#define MSG_LEN       128

#define ZB_PAIRED 1     /* "ZB" flags bit, block boundaries are marker pairs */

void gzblk_msgv(char *msg, const char *fmt, va_list ap);
void gzblk_msg(char *msg, const char *fmt, ...);

/* Growable byte buffer, consumed from the front by moving an offset rather than the bytes. The
   live data is GZBLK_BUF(m), len bytes at p + off, and compaction happens when space is needed. */
typedef struct {
    uint8_t *p;
    size_t len, cap;
    size_t off;
} membuf;

#define GZBLK_BUF(m) ((m)->p + (m)->off)

int gzblk_buf_reserve(membuf *m, size_t need);
int gzblk_buf_append(membuf *m, const uint8_t *data, size_t n);
void gzblk_buf_drop(membuf *m, size_t n);
int gzblk_buf_fill(membuf *m, gzblock_read_fn read, void *ctx, size_t want, int *eof);

/* Returns the header length, 0 if more bytes are needed, (size_t)-1 if this is not a gzip
   header. */
size_t gzblk_header_parse(const uint8_t *buf, size_t len, uint32_t *block_size, uint32_t *zb_flags);

/* How one piece of a block ended, see gzblk_block_feed(). */
enum { SEG_FULL, SEG_END, SEG_SHORT, SEG_OVERFLOW, SEG_ERROR };

/* Incremental decoder for one independent block, fed one piece of input at a time. */
typedef struct {
    zng_stream *z;
    int want_marker;    /* output complete, the trailing empty stored block is still to come */
    int accept_partial; /* the input ends at a marker pair, so any clean output size is a block */
} block_dec;

void gzblk_block_begin(block_dec *d, zng_stream *z, uint8_t *out, uint32_t block_size);
int gzblk_block_feed(block_dec *d, const uint8_t *in, size_t in_len, size_t *used);
const char *gzblk_seg_name(int status);

#include "pool.h"

/* The deflate and inflate codec behind the pool, gzblock_codec() hands it over. */
int gzblock_codec_init(pool_t *p, zng_stream *z);
void gzblock_codec_end(pool_t *p, zng_stream *z);
void gzblock_codec_run(pool_t *p, zng_stream *z, slot_t *slot);

#endif /* GZBLOCK_P_H_ */
