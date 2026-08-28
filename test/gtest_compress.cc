#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "compress.h"
#include "format.h"
#include "options.h"
#include "zlib-ng.h"

namespace {

std::vector<uint8_t> pattern(size_t len) {
    std::vector<uint8_t> data(len);
    for (size_t i = 0; i < len; i++)
        data[i] = static_cast<uint8_t>("a quick brown fox 0123456789 "[i % 29] + (i / 4093) % 5);
    return data;
}

/* A temporary file holding bytes, positioned at its start. */
FILE *stream_of(const std::vector<uint8_t> &bytes) {
    FILE *f = tmpfile();
    EXPECT_NE(nullptr, f);
    EXPECT_EQ(bytes.size(), fwrite(bytes.data(), 1, bytes.size(), f));
    rewind(f);
    return f;
}

std::vector<uint8_t> contents(FILE *f) {
    long len = ftell(f);
    EXPECT_GE(len, 0);
    std::vector<uint8_t> bytes(static_cast<size_t>(len));
    rewind(f);
    EXPECT_EQ(bytes.size(), fread(bytes.data(), 1, bytes.size(), f));
    return bytes;
}

gzng_options defaults() {
    gzng_options opt;
    gzng_options_init(&opt);
    return opt;
}

/* Compress data through the stream function with the given header fields. */
std::vector<uint8_t> compress(const std::vector<uint8_t> &data, const gzng_options &opt, uint32_t mtime = 0,
                              const char *name = nullptr, uint64_t *total_in = nullptr, uint64_t *total_out = nullptr) {
    uint64_t in_count = 0, out_count = 0;
    FILE *in = stream_of(data), *out = tmpfile();
    EXPECT_EQ(0, gzng_compress_stream(in, out, &opt, mtime, name, &in_count, &out_count));
    auto packed = contents(out);
    fclose(in);
    fclose(out);
    if (total_in != nullptr)
        *total_in = in_count;
    if (total_out != nullptr)
        *total_out = out_count;
    return packed;
}

/* What zlib-ng makes of the whole stream, members and all. */
std::vector<uint8_t> inflate(const std::vector<uint8_t> &packed, size_t expect) {
    std::vector<uint8_t> restored(expect + 64);
    zng_stream strm;
    memset(&strm, 0, sizeof(strm));
    EXPECT_EQ(Z_OK, zng_inflateInit2(&strm, MAX_WBITS + 16));
    strm.next_in = packed.data();
    strm.avail_in = static_cast<uint32_t>(packed.size());
    strm.next_out = restored.data();
    strm.avail_out = static_cast<uint32_t>(restored.size());
    EXPECT_EQ(Z_STREAM_END, zng_inflate(&strm, Z_FINISH));
    restored.resize(strm.total_out);
    zng_inflateEnd(&strm);
    return restored;
}

/* Whether the stream carries the marker pair that ends an independent block. */
bool has_block_pairs(const std::vector<uint8_t> &packed) {
    const uint8_t pair[] = {0, 0, 0xff, 0xff, 0, 0, 0, 0xff, 0xff};
    return std::search(packed.begin(), packed.end(), pair, pair + sizeof(pair)) != packed.end();
}

TEST(compress, stream_roundtrips_through_inflate) {
    auto data = pattern(1 << 20);
    uint64_t total_in = 0, total_out = 0;

    auto packed = compress(data, defaults(), 0, nullptr, &total_in, &total_out);
    EXPECT_EQ(data.size(), total_in);
    EXPECT_EQ(packed.size(), total_out);
    EXPECT_LT(packed.size(), data.size());
    EXPECT_EQ(data, inflate(packed, data.size()));
}

TEST(compress, header_records_the_time_and_name) {
    auto packed = compress(pattern(1000), defaults(), 0x5f6a7b8c, "file.txt");
    format_header hdr;

    ASSERT_NE((size_t)-1, format_header_parse(packed.data(), packed.size(), &hdr));
    EXPECT_EQ(0x5f6a7b8cu, hdr.mtime);
    EXPECT_STREQ("file.txt", hdr.name);
    EXPECT_EQ(OS_CODE, packed[9]);
}

TEST(compress, a_plain_stream_has_no_blocks) {
    EXPECT_FALSE(has_block_pairs(compress(pattern(1 << 20), defaults())));
}

TEST(compress, blocksize_cuts_independent_blocks) {
    auto data = pattern(1 << 20);
    gzng_options opt = defaults();
    opt.block_size = 64 << 10;

    auto packed = compress(data, opt);
    EXPECT_TRUE(has_block_pairs(packed));
    EXPECT_EQ(data, inflate(packed, data.size())) << "blocks still make one ordinary member";
}

TEST(compress, processes_ask_for_blocks) {
    gzng_options opt = defaults();
    opt.threads = 2;
    opt.threads_given = 1;

    EXPECT_TRUE(has_block_pairs(compress(pattern(1 << 20), opt)));
}

TEST(compress, levels_trade_size_for_time) {
    auto data = pattern(1 << 20);
    gzng_options fast = defaults(), best = defaults();
    fast.level = 1;
    best.level = 9;

    auto quick = compress(data, fast), small = compress(data, best);
    EXPECT_GT(quick.size(), small.size());
    EXPECT_EQ(data, inflate(quick, data.size()));
    EXPECT_EQ(data, inflate(small, data.size()));
}

TEST(compress, rsyncable_is_deterministic_and_roundtrips) {
    auto data = pattern(1 << 20);
    gzng_options opt = defaults();
    opt.rsyncable = 1;

    auto once = compress(data, opt), twice = compress(data, opt);
    EXPECT_EQ(once, twice);
    EXPECT_EQ(data, inflate(once, data.size()));
}

TEST(compress, empty_input_makes_an_empty_member) {
    auto packed = compress({}, defaults());

    EXPECT_EQ(FORMAT_HEADER_LEN + 2u + FORMAT_TRAILER_LEN, packed.size()) << "the shortest deflate stream is two bytes";
    EXPECT_TRUE(inflate(packed, 0).empty());
}

}  // namespace
