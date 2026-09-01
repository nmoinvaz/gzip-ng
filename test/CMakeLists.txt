enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Google test needs the MSAN instrumented C++ runtime, set up before it is fetched.
if(GZNG_SANITIZER STREQUAL "Memory")
    if(NOT DEFINED ENV{LLVM_BUILD_DIR})
        message(FATAL_ERROR "MSAN needs instrumented C++ libraries, set LLVM_BUILD_DIR")
    endif()
    include_directories($ENV{LLVM_BUILD_DIR}/include $ENV{LLVM_BUILD_DIR}/include/c++/v1)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
elseif(NOT TARGET GTest::GTest)
    find_package(GTest QUIET)
endif()

if(NOT TARGET GTest::GTest)
    include(FetchContent)

    # Prevent overriding the parent project's compiler/linker settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL
        "Use shared (DLL) run-time lib even when Google Test is built as static lib." FORCE)

    if(NOT DEFINED GTEST_TAG)
        set(GTEST_TAG v1.18.0)
    endif()
    message(STATUS "Git checking out GoogleTest ${GTEST_TAG}")
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG ${GTEST_TAG})
    FetchContent_MakeAvailable(googletest)
    add_library(GTest::GTest ALIAS gtest)
    add_library(GTest::Main ALIAS gtest_main)
endif()

add_executable(gtest_gzng
    gtest_block.cc
    gtest_compress.cc
    gtest_decompress.cc
    gtest_format.cc
    gtest_gzng.cc
    gtest_options.cc
    ${CMAKE_SOURCE_DIR}/options.c)
target_link_libraries(gtest_gzng PRIVATE gzng GTest::Main)
if(GZNG_SANITIZER STREQUAL "Memory")
    target_link_directories(gtest_gzng PRIVATE $ENV{LLVM_BUILD_DIR}/lib)
    target_link_options(gtest_gzng PRIVATE -stdlib=libc++ -lc++abi)
endif()
if(BUILD_SHARED_LIBS)
    # The format helpers are not part of the exported API, so the tests build their own copy.
    target_sources(gtest_gzng PRIVATE ${CMAKE_SOURCE_DIR}/format.c)
endif()
add_test(NAME gtest_gzng COMMAND gtest_gzng)

# Test data
string(REPEAT "the quick brown fox jumps over the lazy dog 0123456789 " 20000 GZNG_TEST_DATA)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/data/lorem.txt "${GZNG_TEST_DATA}")

# Roundtrips through the compress-and-verify harness, gzip cross-checks included.
macro(add_cav_test name)
    add_test(NAME cav-${name} COMMAND ${CMAKE_COMMAND}
        -DTARGET=$<TARGET_FILE:gzip-ng>
        -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/data/lorem.txt
        -DTEST_NAME=${name}
        ${ARGN}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compress-and-verify.cmake)
endmacro()

add_cav_test(default)
add_cav_test(level-1 "-DCOMPRESS_ARGS=-1\;-k\;-c")
add_cav_test(level-9 "-DCOMPRESS_ARGS=-9\;-k\;-c")
add_cav_test(rle "-DCOMPRESS_ARGS=-U\;-k\;-c")
add_cav_test(transparent "-DCOMPRESS_ARGS=-T\;-k\;-c")
add_cav_test(blocks "-DCOMPRESS_ARGS=-b\;64K\;-k\;-c")
add_cav_test(rsyncable "-DCOMPRESS_ARGS=--rsyncable\;-k\;-c")
add_cav_test(rsyncable-blocks "-DCOMPRESS_ARGS=--rsyncable\;-b\;64K\;-k\;-c")
add_cav_test(blocks-threads "-DCOMPRESS_ARGS=-b\;64K\;-p\;3\;-k\;-c" "-DDECOMPRESS_ARGS=-d\;-p\;2\;-k\;-c")

add_test(NAME cli-tests COMMAND ${CMAKE_COMMAND}
    -DEXE=$<TARGET_FILE:gzip-ng> -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/clitest
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cli-tests.cmake)
