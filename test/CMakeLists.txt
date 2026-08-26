enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET GTest::GTest)
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
    gtest_gzng.cc
    gtest_options.cc)
target_link_libraries(gtest_gzng PRIVATE gzng GTest::Main)
add_test(NAME gtest_gzng COMMAND gtest_gzng)
