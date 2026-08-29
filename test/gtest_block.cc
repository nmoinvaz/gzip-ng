#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format.h"
#include "gzblock.h"
#include "zlib-ng.h"

namespace {

std::vector<uint8_t> sample_data(size_t len) {
    std::vector<uint8_t> data(len);
    for (size_t i = 0; i < len; i++)
        data[i] = static_cast<uint8_t>("the quick brown fox jumps over the lazy dog "[i % 44] + (i / 8191) % 7);
    return data;
}

size_t vec_write(void *ctx, const uint8_t *buf, size_t len) {
    auto *v = static_cast<std::vector<uint8_t> *>(ctx);
    v->insert(v->end(), buf, buf + len);
    return len;
}

std::vector<uint8_t> block_compress(const std::vector<uint8_t> &data, uint32_t block_size, int nthreads,
                                    size_t chunk = 65521) {
    std::vector<uint8_t> out;
    gzblock_writer *w =
        gzblock_writer_open(vec_write, &out, Z_DEFAULT_COMPRESSION, Z_DEFAULT_STRATEGY, block_size, nthreads);
    EXPECT_NE(nullptr, w);
    for (size_t pos = 0; pos < data.size(); pos += chunk) {
        size_t n = std::min(chunk, data.size() - pos);
        EXPECT_EQ(0, gzblock_writer_write(w, data.data() + pos, n)) << gzblock_writer_error(w);
    }
    EXPECT_EQ(0, gzblock_writer_finish(w)) << gzblock_writer_error(w);
    gzblock_writer_close(w);
    return out;
}

std::vector<uint8_t> whole_inflate(const std::vector<uint8_t> &packed, size_t expect) {
    std::vector<uint8_t> out(expect + 64);
    zng_stream strm;
    memset(&strm, 0, sizeof(strm));
    EXPECT_EQ(Z_OK, zng_inflateInit2(&strm, MAX_WBITS + 16));
    strm.next_in = packed.data();
    strm.avail_in = static_cast<uint32_t>(packed.size());
    strm.next_out = out.data();
    strm.avail_out = static_cast<uint32_t>(out.size());
    EXPECT_EQ(Z_STREAM_END, zng_inflate(&strm, Z_FINISH));
    out.resize(strm.total_out);
    zng_inflateEnd(&strm);
    return out;
}

TEST(block_writer, header_is_an_ordinary_gzip_header) {
    auto data = sample_data(200000);
    auto packed = block_compress(data, 64 * 1024, 1);
    /* Nothing marks the member as cut into blocks, a reader finds that out by looking. */
    EXPECT_EQ(10u, format_header_parse(packed.data(), packed.size(), nullptr));
    EXPECT_EQ(0, packed[3] & 4) << "no extra field";
}

TEST(block_writer, output_inflates_back) {
    auto data = sample_data(1 << 20);
    auto packed = block_compress(data, 64 * 1024, 1);
    EXPECT_EQ(data, whole_inflate(packed, data.size()));
}

TEST(block_writer, threads_do_not_change_the_bytes) {
    auto data = sample_data(1 << 20);
    auto serial = block_compress(data, 64 * 1024, 1);
    auto threaded = block_compress(data, 64 * 1024, 3);
    EXPECT_EQ(serial, threaded);
}

TEST(block_writer, flush_and_params_inside_a_block) {
    auto data = sample_data(300000);
    std::vector<uint8_t> out;
    gzblock_writer *w = gzblock_writer_open(vec_write, &out, Z_DEFAULT_COMPRESSION, Z_DEFAULT_STRATEGY, 64 * 1024, 2);
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(0, gzblock_writer_write(w, data.data(), 100000)) << gzblock_writer_error(w);
    ASSERT_EQ(0, gzblock_writer_flush(w)) << gzblock_writer_error(w);
    ASSERT_EQ(0, gzblock_writer_setparams(w, 1, Z_DEFAULT_STRATEGY)) << gzblock_writer_error(w);
    ASSERT_EQ(0, gzblock_writer_write(w, data.data() + 100000, data.size() - 100000)) << gzblock_writer_error(w);
    ASSERT_EQ(0, gzblock_writer_finish(w)) << gzblock_writer_error(w);
    gzblock_writer_close(w);
    EXPECT_EQ(data, whole_inflate(out, data.size()));
}

struct MemIn {
    const uint8_t *p;
    size_t len, pos, chunk;
};

size_t mem_read(void *ctx, uint8_t *buf, size_t len) {
    auto *in = static_cast<MemIn *>(ctx);
    size_t n = std::min(std::min(len, in->chunk), in->len - in->pos);
    memcpy(buf, in->p + in->pos, n);
    in->pos += n;
    return n;
}

std::vector<uint8_t> block_read(const std::vector<uint8_t> &packed, int nthreads, uint32_t block_size = 0,
                                size_t io_chunk = 65521) {
    MemIn in{packed.data(), packed.size(), 0, io_chunk};
    gzblock_reader *r = gzblock_reader_open(mem_read, &in, nullptr, 0, block_size, nthreads);
    EXPECT_NE(nullptr, r);
    std::vector<uint8_t> out;
    uint8_t buf[65521];
    for (;;) {
        size_t got = 0;
        EXPECT_EQ(0, gzblock_reader_read(r, buf, sizeof(buf), &got)) << gzblock_reader_error(r);
        if (got == 0)
            break;
        out.insert(out.end(), buf, buf + got);
    }
    gzblock_reader_close(r);
    return out;
}

TEST(block_reader, roundtrip_across_thread_counts) {
    auto data = sample_data(1 << 20);
    auto packed = block_compress(data, 64 * 1024, 2);
    for (int nthreads : {0, 1, 3})
        EXPECT_EQ(data, block_read(packed, nthreads)) << "nthreads " << nthreads;
}

TEST(block_reader, zero_copy_handout) {
    auto data = sample_data(500000);
    auto packed = block_compress(data, 64 * 1024, 2);
    MemIn in{packed.data(), packed.size(), 0, 65521};
    gzblock_reader *r = gzblock_reader_open(mem_read, &in, nullptr, 0, 0, 3);
    ASSERT_NE(nullptr, r);
    std::vector<uint8_t> out;
    for (;;) {
        const uint8_t *p = nullptr;
        size_t n = 0;
        ASSERT_EQ(0, gzblock_reader_next(r, &p, &n)) << gzblock_reader_error(r);
        if (n == 0)
            break;
        out.insert(out.end(), p, p + n);
    }
    gzblock_reader_close(r);
    EXPECT_EQ(data, out);
}

TEST(block_reader, plain_gzip_streams_through) {
    auto data = sample_data(400000);
    std::vector<uint8_t> packed(zng_compressBound(data.size()) + 32);
    zng_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(Z_OK, zng_deflateInit2(&strm, 6, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY));
    strm.next_in = data.data();
    strm.avail_in = static_cast<uint32_t>(data.size());
    strm.next_out = packed.data();
    strm.avail_out = static_cast<uint32_t>(packed.size());
    ASSERT_EQ(Z_STREAM_END, zng_deflate(&strm, Z_FINISH));
    packed.resize(strm.total_out);
    zng_deflateEnd(&strm);
    EXPECT_EQ(data, block_read(packed, 0));
}

TEST(block_reader, concatenated_members) {
    auto data = sample_data(300000);
    auto one = block_compress(data, 64 * 1024, 1);
    std::vector<uint8_t> two = one;
    two.insert(two.end(), one.begin(), one.end());
    std::vector<uint8_t> expect = data;
    expect.insert(expect.end(), data.begin(), data.end());
    EXPECT_EQ(expect, block_read(two, 3));
}

TEST(block_writer, meta_lands_in_the_header) {
    auto data = sample_data(1000);
    std::vector<uint8_t> out;
    gzblock_writer *w = gzblock_writer_open(vec_write, &out, 6, Z_DEFAULT_STRATEGY, 64 * 1024, 1);
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(0, gzblock_writer_meta(w, 12345u, "hello.txt"));
    ASSERT_EQ(0, gzblock_writer_write(w, data.data(), data.size()));
    ASSERT_EQ(0, gzblock_writer_finish(w));
    gzblock_writer_close(w);
    ASSERT_GT(out.size(), 31u);
    EXPECT_EQ(8, out[3]) << "the name flag, and nothing else";
    EXPECT_EQ(12345u, (uint32_t)out[4] | ((uint32_t)out[5] << 8));
    EXPECT_EQ(0, memcmp(out.data() + 10, "hello.txt", 10)) << "the name follows the fixed bytes";
    EXPECT_EQ(data, block_read(out, 1));
}

std::vector<uint8_t> rsync_compress(const std::vector<uint8_t> &data) {
    std::vector<uint8_t> out;
    gzblock_writer *w = gzblock_writer_open(vec_write, &out, 6, Z_DEFAULT_STRATEGY, 64 * 1024, 1);
    EXPECT_NE(nullptr, w);
    EXPECT_EQ(0, gzblock_writer_rsyncable(w, 1));
    EXPECT_EQ(0, gzblock_writer_write(w, data.data(), data.size()));
    EXPECT_EQ(0, gzblock_writer_finish(w));
    gzblock_writer_close(w);
    return out;
}

// Aperiodic but compressible bytes. Periodic sample data walks the rolling hash through a tiny
// orbit that can miss the trigger entirely, which is exactly what this test must not do.
std::vector<uint8_t> varied_data(size_t len) {
    std::vector<uint8_t> data(len);
    uint32_t s = 0x12345678;
    for (size_t i = 0; i < len; i++) {
        s = s * 1664525u + 1013904223u;
        data[i] = static_cast<uint8_t>(0x20 + ((s >> 24) & 0x3f));
    }
    return data;
}

TEST(block_writer, rsyncable_realigns_after_an_edit) {
    auto v1 = varied_data(4 << 20);
    std::vector<uint8_t> v2 = v1;
    std::vector<uint8_t> insert(100, 0x55);
    v2.insert(v2.begin() + v2.size() / 4, insert.begin(), insert.end());

    auto p1 = rsync_compress(v1);
    auto p2 = rsync_compress(v2);
    EXPECT_EQ(v1, block_read(p1, 3));
    EXPECT_EQ(v2, block_read(p2, 3));

    /* The 8 byte trailer always differs, crc and size, so compare ahead of it. */
    size_t common = 0, n1 = p1.size() - 8, n2 = p2.size() - 8;
    while (common < n1 && common < n2 && p1[n1 - 1 - common] == p2[n2 - 1 - common])
        common++;
    EXPECT_GT(common, std::min(n1, n2) / 4) << "tails did not re-align";
}

TEST(block_reader, blocks_larger_than_the_probe_assumes_still_decode) {
    /* Nothing records the block size, so the reader assumes one and finds the blocks far bigger.
       They are pair-terminated, which makes them good at any size, so it has to make room rather
       than give up part way through the member. */
    auto data = varied_data(6 << 20); /* barely compressible, so the segments stay large */
    std::vector<uint8_t> out;
    gzblock_writer *w = gzblock_writer_open(vec_write, &out, 6, Z_DEFAULT_STRATEGY, 512 * 1024, 2);
    ASSERT_NE(nullptr, w);
    /* Content-defined ends, so the blocks vary and an early one can fit the reader's assumption
       while a later one does not, which is past the point where it could start over. */
    ASSERT_EQ(0, gzblock_writer_rsyncable(w, 1));
    ASSERT_EQ(0, gzblock_writer_write(w, data.data(), data.size()));
    ASSERT_EQ(0, gzblock_writer_finish(w));
    gzblock_writer_close(w);
    auto packed = out;

    for (int nthreads : {1, 3})
        EXPECT_EQ(data, block_read(packed, nthreads)) << "nthreads " << nthreads;
}

TEST(block_reader, false_pair_in_a_block_larger_than_the_probe_assumes) {
    /* Stored data can hold the nine bytes of a marker pair by chance. The reader cuts there, finds
       the piece incomplete, and inflates the real block again on its own thread, which has to make
       room the same way a worker does when the block is larger than the size it assumed. */
    auto data = sample_data(1 << 20);
    const uint8_t pair[9] = {0, 0, 0xff, 0xff, 0, 0, 0, 0xff, 0xff};
    memcpy(data.data() + 300000, pair, sizeof(pair));
    std::vector<uint8_t> out;
    gzblock_writer *w = gzblock_writer_open(vec_write, &out, 0, Z_DEFAULT_STRATEGY, 512 * 1024, 1);
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(0, gzblock_writer_write(w, data.data(), data.size()));
    ASSERT_EQ(0, gzblock_writer_finish(w));
    gzblock_writer_close(w);

    for (int nthreads : {1, 3})
        EXPECT_EQ(data, block_read(out, nthreads)) << "nthreads " << nthreads;
}

TEST(block_reader, false_lone_marker_in_a_strict_block) {
    /* Full flushes without pairs cut a member into blocks that must be exactly the size the caller
       names, and a chance marker in stored data looks like an early cut. */
    auto data = sample_data(300000);
    const uint8_t marker[4] = {0, 0, 0xff, 0xff};
    memcpy(data.data() + 20000, marker, sizeof(marker));
    std::vector<uint8_t> packed(zng_compressBound(data.size()) + 1024);
    zng_stream strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(Z_OK, zng_deflateInit2(&strm, 0, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY));
    strm.next_out = packed.data();
    strm.avail_out = static_cast<uint32_t>(packed.size());
    for (size_t pos = 0; pos < data.size(); pos += 65536) {
        size_t n = std::min<size_t>(65536, data.size() - pos);
        strm.next_in = data.data() + pos;
        strm.avail_in = static_cast<uint32_t>(n);
        ASSERT_EQ(pos + n < data.size() ? Z_OK : Z_STREAM_END,
                  zng_deflate(&strm, pos + n < data.size() ? Z_FULL_FLUSH : Z_FINISH));
    }
    packed.resize(strm.total_out);
    zng_deflateEnd(&strm);

    for (int nthreads : {1, 3})
        EXPECT_EQ(data, block_read(packed, nthreads, 65536)) << "nthreads " << nthreads;
}

}  // namespace
