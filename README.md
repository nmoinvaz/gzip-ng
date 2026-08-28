# gzip-ng

A gzip replacement built on [zlib-ng](https://github.com/zlib-ng/zlib-ng), with parallel compression into independent deflate blocks and parallel decompression of block-structured files, including `pigz -i` and `pigz --rsyncable -i` output.

## Goals

- Drop-in for GNU and BSD gzip, flag for flag, distro-safe defaults.
- Decompression parallel by default, output byte-identical at any thread count.
- Compression serial by default, parallel independent blocks whenever threads or a block size are used.
- zlib license.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

zlib-ng and Google Test are fetched automatically when not found.

## Build Options

| CMake                 | Description                                                | Default |
|:----------------------|:-----------------------------------------------------------|---------|
| GZNG_ENABLE_TESTS     | Build the test binaries and register them with ctest        | ON      |
| GZNG_ENABLE_BENCHMARKS| Build the benchmarks using Google Benchmark                 | OFF     |
| GZNG_THREADS          | Compress and decompress blocks on a thread pool             | ON      |
| GZNG_SIMD             | Scan for block boundaries with NEON or SSE2                 | ON      |
| GZNG_SANITIZER        | Build with a sanitizer, Address, Memory, Thread, or Undefined | None    |

## Usage

```
gzip-ng [options] [files...]
```

Compresses files in place, file to file.gz, removing the input unless kept. With no files, or -, it filters stdin to stdout. Options and files mix in any order, short options cluster, and installed as gunzip, zcat, or gzcat it presets -d and -c the way gzip's aliases do.

| Option | Description |
|:-------|:------------|
| `-c` `--stdout` | Write to standard output, keep the files |
| `-d` `--decompress` | Decompress, in parallel for block or pigz -i style input |
| `-f` `--force` | Overwrite outputs, compress to a terminal |
| `-k` `--keep` | Keep input files |
| `-r` `--recursive` | Descend into directories |
| `-t` `--test` | Check integrity without writing |
| `-l` `--list` | List compressed file contents, `-v` adds method, crc, and date |
| `-n` `--no-name` | Do not save or restore the name and time |
| `-N` `--name` | Save and restore the name and time |
| `-m` `--no-time` | Do not save or restore the time, keeping the name |
| `-M` `--time` | Save and restore the time |
| `-v` `--verbose` | Report each file processed |
| `-q` `--quiet` | Suppress warnings |
| `-1` .. `-9` | Compression level, 6 by default |
| `--fast` `--best` | Level 1 and level 9 |
| `-b` `--blocksize` *size* | Average bytes per block, K, M, and G suffixes |
| `-p` `--processes` *n* | Threads to use, which asks for blocks, 0 picks the number of CPUs |
| `-H` `--huffman` | Huffman only strategy |
| `-U` `--rle` | Run length strategy |
| `--filtered` | Filtered strategy |
| `--fixed` | Fixed codes strategy |
| `-T` | Store without compressing |
| `-A` | Text mode, accepted for compatibility |
| `--rsyncable` | Make a plain stream rsync friendly, block output always is |
| `--synchronous` | Write outputs to permanent storage before removing inputs |
| `-h` `--help` | Show the usage summary |
| `-V` `--version` | Show the version |
| `-L` `--license` | Show the license |

Exit status is 0, 1 on errors, 2 on warnings, as gzip behaves.

## How it works

### Parallel compression

- With `--processes` or `--blocksize` the input is cut into blocks, 128 KiB by default, and each block is deflated by a worker thread from an empty dictionary, so no block depends on another.
- A block ends at the first byte past half the block size where the low bits of a rolling hash are zero, or at twice the block size.
- Each block ends with a sync flush and a full flush, two empty stored blocks, the nine bytes `00 00 FF FF 00 00 00 FF FF`, the same shape `pigz --independent` writes.
- The blocks are written in order as one ordinary gzip member, header, blocks back to back, and a trailer whose CRC is the blocks' CRCs combined.

```
header | block 1 … 00 00 FF FF 00 00 00 FF FF | block 2 … 00 00 FF FF 00 00 00 FF FF | … | last block | crc32 size
```

### Parallel decompression

- The reader parses the gzip header and looks at the first megabyte of compressed data for a marker pair. One found means independent blocks and a parallel decode, none means plain serial inflate, as for any gzip file.
- A scanner finds every `00 00 FF FF`, one empty stored block, with SIMD filtering for the zero pair the way `memchr` filters for a byte. A second empty stored block behind it makes a boundary.
- Each segment between boundaries is inflated on its own by a worker into a slot the size of a block, with its CRC taken there, and the blocks are handed out in order.
- `pigz --rsyncable --independent` ends a block at every rsync point, about every 4 KiB. The reader gathers those until about a block size is in hand before handing them to a worker, since a slot per 4 KiB costs more to hand off than to inflate.
- Stored data can hold the marker pair by chance, so a boundary is trusted only when the segment inflates to a whole block. A segment that comes up short is inflated again on the calling thread across the segments after it, and a member with no block structure at all goes back through plain inflate.
- The trailer's CRC and length are checked against what the blocks produced.

## Benchmarks

Engine benchmarks use Google Benchmark, off by default:

```
cmake -B build-bench -D CMAKE_BUILD_TYPE=Release -D GZNG_ENABLE_BENCHMARKS=ON
cmake --build build-bench
build-bench/test/benchmarks/benchmark_gzng
```

Comparing whole binaries is a different job, fork and exec noise belongs to tools built for it. `test/benchmarks/compare.sh [size-MB] [threads]` runs gzip-ng against the system gzip and pigz where installed, through hyperfine when available and a built-in best-of-3 timer otherwise.

### Results

* Apple M5, 10 cores.
* 512 MiB of mixed source, build output, and text, 4.8 to 1 under `gzip -6`.
* zlib-ng 2.3.3, pigz 2.8, gzip 1.14, level 6, `hyperfine` mean of 3.

| Compress | Wall | MiB/s | Output | Of input |
|---|---|---|---|---|
| `gzip-ng -p 10` | 0.39 s | 1330 | 111.9 MiB | 21.9% |
| `pigz -p 10` | 1.02 s | 504 | 107.1 MiB | 20.9% |
| `gzip-ng` | 2.44 s | 210 | 109.9 MiB | 21.5% |
| `minigzip` | 2.47 s | 208 | 109.9 MiB | 21.5% |
| `gzip -6` | 6.35 s | 81 | 107.3 MiB | 21.0% |

| Decompress | Input | Wall | MiB/s |
|---|---|---|---|
| `gzip-ng -p 10` | from `gzip-ng -p 10` | 0.07 s | 7570 |
| `pigz` | from `pigz -p 10` | 0.44 s | 1170 |
| `gzip-ng` | from `gzip-ng` | 0.45 s | 1130 |
| `minigzip` | from `minigzip` | 0.47 s | 1090 |
| `gzip` | from `gzip -6` | 1.06 s | 480 |
