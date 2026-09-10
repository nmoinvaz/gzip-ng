#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "gtest_block.h"
#include "gzblock.h"
#include "zlib-ng.h"

namespace {

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
    gzblock_reader *r = gzblock_reader_open(mem_read, &in, nullptr, 0, 3);
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

TEST(block_reader, lone_flush_markers_are_not_boundaries) {
    /* A sync flush writes the same marker as a full flush and keeps the dictionary, so a member of
       lone markers says nothing about independent blocks and has to inflate as one stream. */
    auto data = sample_data(1 << 20);
    for (int flush : {Z_SYNC_FLUSH, Z_FULL_FLUSH}) {
        std::vector<uint8_t> packed(zng_compressBound(data.size()) + 1024);
        zng_stream strm;
        memset(&strm, 0, sizeof(strm));
        ASSERT_EQ(Z_OK, zng_deflateInit2(&strm, 6, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY));
        strm.next_out = packed.data();
        strm.avail_out = static_cast<uint32_t>(packed.size());
        for (size_t pos = 0; pos < data.size(); pos += 65536) {
            size_t n = std::min<size_t>(65536, data.size() - pos);
            strm.next_in = data.data() + pos;
            strm.avail_in = static_cast<uint32_t>(n);
            ASSERT_EQ(pos + n < data.size() ? Z_OK : Z_STREAM_END,
                      zng_deflate(&strm, pos + n < data.size() ? flush : Z_FINISH));
        }
        packed.resize(strm.total_out);
        zng_deflateEnd(&strm);

        for (int nthreads : {1, 3})
            EXPECT_EQ(data, block_read(packed, nthreads)) << "flush " << flush << " nthreads " << nthreads;
    }
}

TEST(block_reader, flushed_blocks_of_any_length) {
    /* A flush or a settings change ends the block early, down to a single byte, and the reader has
       to take each as a block. */
    auto data = varied_data(3 << 20);
    for (int rsync : {0, 1}) {
        std::vector<uint8_t> out;
        gzblock_writer *w = gzblock_writer_open(vec_write, &out, 6, Z_DEFAULT_STRATEGY, 64 * 1024, 2);
        ASSERT_NE(nullptr, w);
        ASSERT_EQ(0, gzblock_writer_rsyncable(w, rsync));
        size_t pos = 0;
        for (size_t cut : {size_t(1), size_t(7), size_t(5000), size_t(70000), size_t(200001), data.size()}) {
            ASSERT_EQ(0, gzblock_writer_write(w, data.data() + pos, cut - pos));
            ASSERT_EQ(0, gzblock_writer_flush(w));
            ASSERT_EQ(0, gzblock_writer_setparams(w, cut % 2 ? 1 : 9, Z_DEFAULT_STRATEGY));
            pos = cut;
        }
        ASSERT_EQ(0, gzblock_writer_finish(w));
        gzblock_writer_close(w);
        EXPECT_EQ(data, whole_inflate(out, data.size()));
        for (int nthreads : {1, 3})
            EXPECT_EQ(data, block_read(out, nthreads)) << "rsync " << rsync << " nthreads " << nthreads;
    }
}

}  // namespace
