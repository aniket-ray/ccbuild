cmake_minimum_required(VERSION 3.15)
project(ccbuild CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ninja core - static library from ninja submodule (POSIX/Linux)
add_library(ninjacore STATIC
        ninja/src/build_log.cc
        ninja/src/build.cc
        ninja/src/clean.cc
        ninja/src/clparser.cc
        ninja/src/debug_flags.cc
        ninja/src/depfile_parser.cc
        ninja/src/deps_log.cc
        ninja/src/disk_interface.cc
        ninja/src/dyndep.cc
        ninja/src/dyndep_parser.cc
        ninja/src/edit_distance.cc
        ninja/src/elide_middle.cc
        ninja/src/eval_env.cc
        ninja/src/explanations.cc
        ninja/src/graph.cc
        ninja/src/graphviz.cc
        ninja/src/jobserver.cc
        ninja/src/jobserver-posix.cc
        ninja/src/json.cc
        ninja/src/lexer.cc
        ninja/src/line_printer.cc
        ninja/src/manifest_parser.cc
        ninja/src/metrics.cc
        ninja/src/missing_deps.cc
        ninja/src/parser.cc
        ninja/src/real_command_runner.cc
        ninja/src/state.cc
        ninja/src/status_printer.cc
        ninja/src/string_piece_util.cc
        ninja/src/subprocess-posix.cc
        ninja/src/util.cc
        ninja/src/version.cc
)

target_include_directories(ninjacore PUBLIC ninja/src)
target_compile_options(ninjacore PRIVATE -w)

# ninja uses ppoll on Linux when available
include(CheckCXXSymbolExists)
check_cxx_symbol_exists(ppoll poll.h HAVE_PPOLL)
if (HAVE_PPOLL)
    target_compile_definitions(ninjacore PRIVATEW USE_PPOLL=1)
endif ()

# ccbuild library
add_library(ccbuildlib STATIC
        src/core/target.cc
)

target_include_directories(ccbuildlib
        PUBLIC include
        PUBLIC ninja/src
        PRIVATE src
)
target_compile_features(ccbuildlib PUBLIC cxx_std_20)
target_link_libraries(ccbuildlib PUBLIC ninjacorea)

# ccbuild cli
add_executable(ccbuild src/main.cc)
target_link_libraries(ccbuild PRIVATE ccbuildlib)

target_compile_definitions(ccbuild PRIVATE
        CCBUILD_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
        CCBUILD_LIB_DIR="${CMAKE_BINARY_DIR}"
        CCBUILD_NINJA_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/ninja/src"
)

# tests
include(CTest)
if (BUILD_TESTING)
    include(FetchContent)
    if (POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif ()
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
            URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(ccbuild_tests
            tests/core/target_test.cc
    )

    target_link_libraries(ccbuild_tests PRIVATE ccbuildlib GTest::gtest_main)
    target_include_directories(ccbuild_tests PRIVATE src)
    include(GoogleTest)
    gtest_discover_tests(ccbuild_tests)
endif ()



