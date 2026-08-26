// Marker scanner throughput and the variants that lost to what production runs. The haystack is
// real block writer output, so zero density and marker spacing match what the reader scans, and
// every variant reports its hit count so a faster one that misses markers cannot hide.
//
// The replica scan_neon1 exists to keep this file honest, it must track BM_scan_marker or the
// comparisons below mean nothing. Measured on a 10 core Apple Silicon, median of 8:
//
//   scan_marker (production)   10.73 GiB/s
//   neon1 byte chain           10.68   the replica, matching production
//   neon1 32 bit compare       10.41   one unaligned compare instead of three bytes, slower
//   neon2 two vectors          9.77    one filter branch per 32 bytes
//   neon4 four vectors         9.23    one filter branch per 64 bytes
//   scalar, either check       8.21    memchr dominates, the candidate check does not matter
//
// Unrolling loses because the filter branch was never the cost. Compressed bytes hold about one
// zero per 256, so the branch is predicted not taken about 94 percent of the time, while the
// combined minimum adds work to every iteration and doubles or quadruples the chance of paying
// for the slow path.
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
        gzblock_writer *w = gzblock_writer_open(vec_write, &out, 6, Z_DEFAULT_STRATEGY, 128 * 1024, 0);
        gzblock_writer_write(w, data.data(), data.size());
        gzblock_writer_finish(w);
        gzblock_writer_close(w);
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
        state.counters["hits"] = static_cast<double>(hits);
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

inline uint64_t nibble_mask(uint8x16_t zero) {
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(zero), 4)), 0);
}

template <bool (*Check)(const uint8_t *)>
inline const uint8_t *candidates(const uint8_t *base, uint64_t mask) {
    do {
        unsigned i = (unsigned)(__builtin_ctzll(mask) >> 2);
        const uint8_t *q = base + i;
        if (Check(q))
            return q;
        mask &= ~(0xfull << (i * 4));   /* clear the whole nibble, as production does */
    } while (mask != 0);
    return NULL;
}

/* Faithful copy of the production loop, one vector per iteration. Matching production's speed
   is what makes the variants below worth believing. */
template <bool (*Check)(const uint8_t *)>
const uint8_t *scan_neon1(const uint8_t *p, const uint8_t *end) {
    while (end - p >= 16) {
        uint8x16_t v = vld1q_u8(p);
        if (vminvq_u8(v) == 0) {
            const uint8_t *q = candidates<Check>(p, nibble_mask(vceqzq_u8(v)));
            if (q != NULL)
                return q;
        }
        p += 16;
    }
    return scan_scalar<Check>(p, end);
}

/* Two vectors per iteration, one combined filter branch per 32 bytes. */
const uint8_t *scan_neon2(const uint8_t *p, const uint8_t *end) {
    while (end - p >= 32) {
        uint8x16_t v0 = vld1q_u8(p), v1 = vld1q_u8(p + 16);
        if (vminvq_u8(vminq_u8(v0, v1)) == 0) {
            uint64_t m0 = nibble_mask(vceqzq_u8(v0));
            if (m0 != 0) {
                const uint8_t *q = candidates<check_bytes>(p, m0);
                if (q != NULL)
                    return q;
            }
            uint64_t m1 = nibble_mask(vceqzq_u8(v1));
            if (m1 != 0) {
                const uint8_t *q = candidates<check_bytes>(p + 16, m1);
                if (q != NULL)
                    return q;
            }
        }
        p += 32;
    }
    return scan_neon1<check_bytes>(p, end);
}

/* Four vectors per iteration, a min tree and one branch per 64 bytes. */
const uint8_t *scan_neon4(const uint8_t *p, const uint8_t *end) {
    while (end - p >= 64) {
        uint8x16_t v0 = vld1q_u8(p), v1 = vld1q_u8(p + 16);
        uint8x16_t v2 = vld1q_u8(p + 32), v3 = vld1q_u8(p + 48);
        if (vminvq_u8(vminq_u8(vminq_u8(v0, v1), vminq_u8(v2, v3))) == 0) {
            const uint8x16_t vs[4] = {v0, v1, v2, v3};
            for (int k = 0; k < 4; k++) {
                uint64_t m = nibble_mask(vceqzq_u8(vs[k]));
                if (m != 0) {
                    const uint8_t *q = candidates<check_bytes>(p + 16 * k, m);
                    if (q != NULL)
                        return q;
                }
            }
        }
        p += 64;
    }
    return scan_neon1<check_bytes>(p, end);
}

void BM_scan_neon1_bytes(benchmark::State &state) { scan_all<scan_neon1<check_bytes>>(state); }
BENCHMARK(BM_scan_neon1_bytes);

void BM_scan_neon1_u32(benchmark::State &state) { scan_all<scan_neon1<check_u32>>(state); }
BENCHMARK(BM_scan_neon1_u32);

void BM_scan_neon2(benchmark::State &state) { scan_all<scan_neon2>(state); }
BENCHMARK(BM_scan_neon2);

void BM_scan_neon4(benchmark::State &state) { scan_all<scan_neon4>(state); }
BENCHMARK(BM_scan_neon4);
#endif

}  // namespace
