#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "gzng.h"
#include "zlib-ng.h"

TEST(version, reported) {
    EXPECT_STREQ(gzng_version(), GZNG_VERSION);
    EXPECT_GT(strlen(gzng_zlibng_version()), 0u);
}

TEST(zlibng, roundtrip) {
    const std::string text(100000, 'a');
    std::vector<uint8_t> packed(zng_compressBound(text.size()));
    size_t packed_len = packed.size();
    ASSERT_EQ(Z_OK, zng_compress(packed.data(), &packed_len,
                                 reinterpret_cast<const uint8_t *>(text.data()), text.size()));
    ASSERT_LT(packed_len, text.size());

    std::vector<uint8_t> restored(text.size());
    size_t restored_len = restored.size();
    ASSERT_EQ(Z_OK, zng_uncompress(restored.data(), &restored_len, packed.data(), packed_len));
    ASSERT_EQ(text.size(), restored_len);
    EXPECT_EQ(0, memcmp(text.data(), restored.data(), restored_len));
}
