// Marker scanner throughput, the production scan against the scalar memchr loop it replaces.
// The haystack is real block writer output, so zero density and marker spacing match what the
// reader scans, and each reports its hit count so a faster scan that misses markers cannot hide.
//
// Measured on a 10 core Apple Silicon, median of 8, with the variants that lost along the way:
//
//   scan_marker (production)   10.73 GiB/s
//   neon 32 bit compare        10.41   one unaligned compare instead of three bytes, slower
//   neon two vectors           9.77    one filter branch per 32 bytes
//   neon four vectors          9.23    one filter branch per 64 bytes
//   scalar                     8.21    memchr dominates, the candidate check does not matter
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

/* The scalar loop the vector scan falls back to, memchr for the zero and a byte check. */
const uint8_t *scan_scalar(const uint8_t *p, const uint8_t *end) {
    while (p < end && (p = (const uint8_t *)memchr(p, 0, (size_t)(end - p))) != NULL) {
        if (p[1] == 0 && p[2] == 0xff && p[3] == 0xff)
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

void BM_scan_marker(benchmark::State &state) {
    scan_all<scan_marker>(state);
}
BENCHMARK(BM_scan_marker);

void BM_scan_scalar(benchmark::State &state) {
    scan_all<scan_scalar>(state);
}
BENCHMARK(BM_scan_scalar);

}  // namespace
