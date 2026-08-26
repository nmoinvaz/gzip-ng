// Engine throughput benchmarks. Thread counts are real-time measured, the pool does the work on
// other threads, so wall clock is the meaningful axis.
#include <cstdint>
#include <cstring>
#include <vector>

#include <benchmark/benchmark.h>

#include "gzblock.h"
#include "zlib-ng.h"

namespace {

constexpr size_t kDataLen = 64 << 20;
constexpr uint32_t kBlock = 128 * 1024;

const std::vector<uint8_t> &sample_data() {
    static const std::vector<uint8_t> data = [] {
        std::vector<uint8_t> d(kDataLen);
        for (size_t i = 0; i < d.size(); i++)
            d[i] = static_cast<uint8_t>("the quick brown fox jumps over the lazy dog "[i % 44] +
                                        (i / 8191) % 7);
        return d;
    }();
    return data;
}

size_t null_write(void *, const uint8_t *, size_t len) {
    return len;
}

size_t vec_write(void *ctx, const uint8_t *buf, size_t len) {
    auto *v = static_cast<std::vector<uint8_t> *>(ctx);
    v->insert(v->end(), buf, buf + len);
    return len;
}

struct MemIn {
    const uint8_t *p;
    size_t len, pos;
};

size_t mem_read(void *ctx, uint8_t *buf, size_t len) {
    auto *in = static_cast<MemIn *>(ctx);
    size_t n = std::min(len, in->len - in->pos);
    memcpy(buf, in->p + in->pos, n);
    in->pos += n;
    return n;
}

const std::vector<uint8_t> &block_packed() {
    static const std::vector<uint8_t> packed = [] {
        std::vector<uint8_t> out;
        gzblock_writer *w = gzblock_wopen(vec_write, &out, 6, Z_DEFAULT_STRATEGY, kBlock, 0);
        gzblock_write(w, sample_data().data(), sample_data().size());
        gzblock_wfinish(w);
        gzblock_wclose(w);
        return out;
    }();
    return packed;
}

const std::vector<uint8_t> &plain_packed() {
    static const std::vector<uint8_t> packed = [] {
        const auto &data = sample_data();
        std::vector<uint8_t> out(zng_compressBound(data.size()) + 32);
        zng_stream z;
        memset(&z, 0, sizeof(z));
        zng_deflateInit2(&z, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
        z.next_in = data.data();
        z.avail_in = static_cast<uint32_t>(data.size());
        z.next_out = out.data();
        z.avail_out = static_cast<uint32_t>(out.size());
        zng_deflate(&z, Z_FINISH);
        out.resize(z.total_out);
        zng_deflateEnd(&z);
        return out;
    }();
    return packed;
}

void read_all(const std::vector<uint8_t> &packed, int nthreads) {
    MemIn in{packed.data(), packed.size(), 0};
    gzblock_reader *r = gzblock_ropen(mem_read, &in, nullptr, 0, 0, nthreads);
    for (;;) {
        const uint8_t *p;
        size_t n;
        if (gzblock_rnext(r, &p, &n) != 0 || n == 0)
            break;
        benchmark::DoNotOptimize(p);
    }
    gzblock_rclose(r);
}

void BM_block_compress(benchmark::State &state) {
    const auto &data = sample_data();
    for (auto _ : state) {
        gzblock_writer *w = gzblock_wopen(null_write, nullptr, 6, Z_DEFAULT_STRATEGY, kBlock,
                                          static_cast<int>(state.range(0)));
        gzblock_write(w, data.data(), data.size());
        gzblock_wfinish(w);
        gzblock_wclose(w);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(data.size()));
}
BENCHMARK(BM_block_compress)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->UseRealTime()
    ->Unit(benchmark::kMillisecond);

void BM_block_decompress(benchmark::State &state) {
    const auto &packed = block_packed();
    for (auto _ : state)
        read_all(packed, static_cast<int>(state.range(0)));
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(kDataLen));
}
BENCHMARK(BM_block_decompress)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->UseRealTime()
    ->Unit(benchmark::kMillisecond);

void BM_plain_decompress(benchmark::State &state) {
    const auto &packed = plain_packed();
    for (auto _ : state)
        read_all(packed, 1);
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(kDataLen));
}
BENCHMARK(BM_plain_decompress)->UseRealTime()->Unit(benchmark::kMillisecond);

}  // namespace
