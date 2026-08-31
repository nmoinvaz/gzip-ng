#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "compress.h"
#include "decompress.h"
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
    if (!bytes.empty())
        EXPECT_EQ(bytes.size(), fwrite(bytes.data(), 1, bytes.size(), f));
    rewind(f);
    return f;
}

std::vector<uint8_t> contents(FILE *f) {
    long len = ftell(f);
    EXPECT_GE(len, 0);
    std::vector<uint8_t> bytes(static_cast<size_t>(len));
    rewind(f);
    if (!bytes.empty())
        EXPECT_EQ(bytes.size(), fread(bytes.data(), 1, bytes.size(), f));
    return bytes;
}

/* One plain gzip member, the way gzip itself writes one. */
std::vector<uint8_t> gzip(const std::vector<uint8_t> &data) {
    std::vector<uint8_t> packed(zng_compressBound(data.size()) + 32);
    zng_stream strm;
    memset(&strm, 0, sizeof(strm));
    EXPECT_EQ(Z_OK, zng_deflateInit2(&strm, 6, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY));
    strm.next_in = data.data();
    strm.avail_in = static_cast<uint32_t>(data.size());
    strm.next_out = packed.data();
    strm.avail_out = static_cast<uint32_t>(packed.size());
    EXPECT_EQ(Z_STREAM_END, zng_deflate(&strm, Z_FINISH));
    packed.resize(strm.total_out);
    zng_deflateEnd(&strm);
    return packed;
}

/* A member cut into independent blocks, as --blocksize and --processes write it. */
std::vector<uint8_t> blocks(const std::vector<uint8_t> &data) {
    gzng_options opt;
    gzng_options_init(&opt);
    opt.block_size = 64 << 10;
    opt.threads = 2;
    opt.threads_given = 1;
    FILE *in = stream_of(data), *out = tmpfile();
    uint64_t total_in = 0, total_out = 0;
    EXPECT_EQ(0,
              gzng_compress_stream(in, out, opt.level, opt.strategy, opt.block_size, opt.threads, opt.rsyncable, 0,
                                   nullptr, &total_in, &total_out));
    auto packed = contents(out);
    fclose(in);
    fclose(out);
    return packed;
}

struct run_result {
    int rc;
    uint64_t total_in, total_out;
    std::vector<uint8_t> output;
};

/* Decompress bytes through the stream function, into a file unless to_null, after any head bytes
   handed over as already read. */
run_result run(const std::vector<uint8_t> &input, const gzng_options &opt, bool to_null = false,
               const std::vector<uint8_t> &head = {}) {
    run_result r = {0, 0, 0, {}};
    FILE *in = stream_of(input), *out = to_null ? nullptr : tmpfile();
    r.rc = gzng_decompress_stream(in, out, head.data(), head.size(), opt.block_size, opt.threads, &r.total_in,
                                  &r.total_out);
    if (out != nullptr) {
        r.output = contents(out);
        fclose(out);
    }
    fclose(in);
    return r;
}

gzng_options defaults() {
    gzng_options opt;
    gzng_options_init(&opt);
    opt.decompress = 1;
    return opt;
}

TEST(decompress, plain_gzip_roundtrips) {
    auto data = pattern(1 << 20);
    auto packed = gzip(data);

    auto r = run(packed, defaults());
    ASSERT_EQ(0, r.rc);
    EXPECT_EQ(data, r.output);
    EXPECT_EQ(packed.size(), r.total_in);
    EXPECT_EQ(data.size(), r.total_out);
}

TEST(decompress, block_stream_roundtrips_on_threads) {
    auto data = pattern(3 << 20);
    auto packed = blocks(data);
    gzng_options opt = defaults();
    opt.threads = 3;
    opt.threads_given = 1;

    auto r = run(packed, opt);
    ASSERT_EQ(0, r.rc);
    EXPECT_EQ(data, r.output);
    EXPECT_EQ(data.size(), r.total_out);
}

TEST(decompress, members_concatenate) {
    auto first = pattern(100000), second = pattern(70000);
    auto packed = gzip(first);
    auto more = gzip(second);
    packed.insert(packed.end(), more.begin(), more.end());
    auto expected = first;
    expected.insert(expected.end(), second.begin(), second.end());

    auto r = run(packed, defaults());
    ASSERT_EQ(0, r.rc);
    EXPECT_EQ(expected, r.output);
}

TEST(decompress, no_output_still_counts) {
    auto data = pattern(200000);

    auto r = run(gzip(data), defaults(), true);
    ASSERT_EQ(0, r.rc);
    EXPECT_EQ(data.size(), r.total_out);
}

TEST(decompress, other_data_passes_through) {
    auto data = pattern(50000);

    auto r = run(data, defaults());
    ASSERT_EQ(0, r.rc);
    EXPECT_EQ(data, r.output) << "input that is not gzip comes out unchanged";
}

TEST(decompress, head_bytes_come_before_the_stream) {
    auto data = pattern(50000);
    auto packed = gzip(data);
    std::vector<uint8_t> head(packed.begin(), packed.begin() + 2), rest(packed.begin() + 2, packed.end());

    auto r = run(rest, defaults(), false, head);
    ASSERT_EQ(0, r.rc);
    EXPECT_EQ(data, r.output);
    EXPECT_EQ(packed.size(), r.total_in) << "the head bytes count as input";
}

TEST(decompress, truncated_input_fails) {
    auto packed = gzip(pattern(300000));
    packed.resize(packed.size() / 2);

    EXPECT_EQ(-1, run(packed, defaults()).rc);
}

TEST(decompress, empty_input_is_empty_output) {
    auto r = run({}, defaults());
    ASSERT_EQ(0, r.rc);
    EXPECT_TRUE(r.output.empty());
    EXPECT_EQ(0u, r.total_out);
}

}  // namespace
