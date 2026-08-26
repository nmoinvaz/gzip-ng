// Marker scanner throughput, and the candidate check A/B, the byte chain against one
// unaligned 32 bit compare. The haystack is real block writer output, so zero density and
// marker spacing match what the reader actually scans.
#include <cstdint>
#include <cstring>
#include <vector>

#include <benchmark/benchmark.h>

#include "gzblock.h"
#include "scanner.h"
#include "zlib-ng.h"

namespace {

size_t vec_write(void *ctx, const uint8_t *buf, size_t len) {
    auto *v = static_cast<std::vector<uint8_t> *>(ctx);
    v->insert(v->end(), buf, buf + len);
    return len;
}

const std::vector<uint8_t> &haystack() {
    static const std::vector<uint8_t> packed = [] {
        std::vector<uint8_t> data(64 << 20);
        uint32_t s = 0x2545f491;
        for (size_t i = 0; i < data.size(); i++) {
            s = s * 1664525u + 1013904223u;
            data[i] = static_cast<uint8_t>(0x20 + ((s >> 24) & 0x3f));
        }
        std::vector<uint8_t> out;
        gzblock_writer *w = gzblock_wopen(vec_write, &out, 6, Z_DEFAULT_STRATEGY, 128 * 1024, 0);
        gzblock_write(w, data.data(), data.size());
        gzblock_wfinish(w);
        gzblock_wclose(w);
        return out;
    }();
    return packed;
}

inline bool check_bytes(const uint8_t *q) {
    return q[1] == 0 && q[2] == 0xff && q[3] == 0xff;
}

inline bool check_u32(const uint8_t *q) {
    uint32_t v;
    memcpy(&v, q, 4);
    return v == 0xffff0000u;
}

template <bool (*Check)(const uint8_t *)>
const uint8_t *scan_scalar(const uint8_t *p, const uint8_t *end) {
    while (p < end && (p = (const uint8_t *)memchr(p, 0, (size_t)(end - p))) != NULL) {
        if (Check(p))
            return p;
        p++;
    }
    return NULL;
}

template <const uint8_t *(*Scan)(const uint8_t *, const uint8_t *)>
void scan_all(benchmark::State &state) {
    const auto &h = haystack();
    const uint8_t *base = h.data(), *end = base + h.size() - 3;
    for (auto _ : state) {
        size_t hits = 0;
        for (const uint8_t *p = base; (p = Scan(p, end)) != NULL; p++)
            hits++;
        benchmark::DoNotOptimize(hits);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(h.size()));
}

void BM_scan_marker(benchmark::State &state) { scan_all<scan_marker>(state); }
BENCHMARK(BM_scan_marker);

void BM_scan_scalar_bytes(benchmark::State &state) { scan_all<scan_scalar<check_bytes>>(state); }
BENCHMARK(BM_scan_scalar_bytes);

void BM_scan_scalar_u32(benchmark::State &state) { scan_all<scan_scalar<check_u32>>(state); }
BENCHMARK(BM_scan_scalar_u32);

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>

template <bool (*Check)(const uint8_t *)>
const uint8_t *scan_neon(const uint8_t *p, const uint8_t *end) {
    while (p + 16 + 3 <= end) {
        uint8x16_t v = vld1q_u8(p);
        if (vminvq_u8(v) == 0) {
            uint8x16_t zero = vceqzq_u8(v);
            uint64_t mask = vget_lane_u64(
                vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(zero), 4)), 0);
            while (mask != 0) {
                unsigned i = (unsigned)(__builtin_ctzll(mask) >> 2);
                const uint8_t *q = p + i;
                if (Check(q))
                    return q;
                mask &= mask - 1;
            }
        }
        p += 16;
    }
    return scan_scalar<Check>(p, end);
}

void BM_scan_neon_bytes(benchmark::State &state) { scan_all<scan_neon<check_bytes>>(state); }
BENCHMARK(BM_scan_neon_bytes);

void BM_scan_neon_u32(benchmark::State &state) { scan_all<scan_neon<check_u32>>(state); }
BENCHMARK(BM_scan_neon_u32);
#endif

}  // namespace
