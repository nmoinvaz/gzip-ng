# Engine benchmarks built on Google Benchmark, following the zlib-ng convention.
include(FetchContent)

enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(benchmark 1.9.2 QUIET)
if(NOT benchmark_FOUND)
    set(BENCHMARK_ENABLE_TESTING OFF)
    if(NOT DEFINED GBENCHMARK_TAG)
        set(GBENCHMARK_TAG v1.9.5)
    endif()
    message(STATUS "Git checking out Google Benchmark ${GBENCHMARK_TAG}")
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG ${GBENCHMARK_TAG})
    FetchContent_MakeAvailable(benchmark)
endif()

add_executable(benchmark_gzng
    benchmark_gzblock.cc
    benchmark_scanner.cc)
target_link_libraries(benchmark_gzng PRIVATE gzng benchmark::benchmark benchmark::benchmark_main)
