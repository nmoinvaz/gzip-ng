#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "compress.h"
#include "options.h"
#include "zlib-ng.h"

namespace {

std::vector<uint8_t> pattern(size_t len) {
    std::vector<uint8_t> data(len);
    for (size_t i = 0; i < len; i++)
        data[i] = static_cast<uint8_t>("a quick brown fox 0123456789 "[i % 29] + (i / 4093) % 5);
    return data;
}

TEST(compress, stream_roundtrips_through_inflate) {
    auto data = pattern(1 << 20);
    FILE *in = tmpfile(), *out = tmpfile();
    ASSERT_NE(nullptr, in);
    ASSERT_NE(nullptr, out);
    ASSERT_EQ(data.size(), fwrite(data.data(), 1, data.size(), in));
    rewind(in);

    gzng_options opt;
    gzng_options_init(&opt);
    ASSERT_EQ(0, gzng_compress_stream(in, out, &opt));

    long packed_len = ftell(out);
    ASSERT_GT(packed_len, 0);
    ASSERT_LT(static_cast<size_t>(packed_len), data.size());
    rewind(out);
    std::vector<uint8_t> packed(static_cast<size_t>(packed_len));
    ASSERT_EQ(packed.size(), fread(packed.data(), 1, packed.size(), out));
    fclose(in);
    fclose(out);

    std::vector<uint8_t> restored(data.size() + 64);
    zng_stream z;
    memset(&z, 0, sizeof(z));
    ASSERT_EQ(Z_OK, zng_inflateInit2(&z, 15 + 16));
    z.next_in = packed.data();
    z.avail_in = static_cast<uint32_t>(packed.size());
    z.next_out = restored.data();
    z.avail_out = static_cast<uint32_t>(restored.size());
    ASSERT_EQ(Z_STREAM_END, zng_inflate(&z, Z_FINISH));
    restored.resize(z.total_out);
    zng_inflateEnd(&z);
    EXPECT_EQ(data, restored);
}

}  // namespace
