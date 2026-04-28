cmake_minimum_required(VERSION 3.15)
project(ccbuild CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# code coverage (-DCCBUILD_COVERAGE=ON)
# Flags applied per-target (ccbuildlib, ninjacore, ccbuild_tests) so that the
# ccbuild CLI binary and the libraries it ships for end-user linking are NOT
# polluted with profiler runtime references.
option(CCBUILD_COVERAGE "Enable code coverage instrumentation" ON)
if (CCBUILD_COVERAGE)
    message(STATUS "Code coverage enabled")
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        message(STATUS "   Using Clang source-based coverage (llvm-cov)")
        set(CCBUILD_COVERAGE_COMPILE_FLAGS -fprofile-instr-generate -fcoverage-mapping)
        set(CCBUILD_COVERAGE_LINK_FLAGS -fprofile-instr-generate)
        set(CCBUILD_COVERAGE_CLANG TRUE)
    else ()
        message(STATUS "   Using GCC gcov coverage (lcov)")
        set(CCBUILD_COVERAGE_COMPILE_FLAGS --coverage -fprofile-arcs -ftest-coverage)
        set(CCBUILD_COVERAGE_LINK_FLAGS --coverage)
        set(CCBUILD_COVERAGE_CLANG FALSE)
    endif ()
endif ()

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
if (CCBUILD_COVERAGE)
    target_compile_options(ninjacore PRIVATE ${CCBUILD_COVERAGE_COMPILE_FLAGS})
    target_link_options(ninjacore PRIVATE ${CCBUILD_COVERAGE_LINK_FLAGS})
endif ()

# ninja uses ppoll on Linux when available
include(CheckCXXSymbolExists)
check_cxx_symbol_exists(ppoll poll.h HAVE_PPOLL)
if (HAVE_PPOLL)
    target_compile_definitions(ninjacore PRIVATE USE_PPOLL=1)
endif ()

# ccbuild library
add_library(ccbuildlib STATIC
        src/core/target.cc
        src/targets/executable.cc
        src/targets/static_library.cc
        src/internal/ninja_bridge.cc
        src/internal/compiler.cc
        src/internal/compiler.h
        src/project/project.cc
)

target_include_directories(ccbuildlib
        PUBLIC include
        PUBLIC ninja/src
        PRIVATE src
)
target_compile_features(ccbuildlib PUBLIC cxx_std_20)
target_link_libraries(ccbuildlib PUBLIC ninjacore)
if (CCBUILD_COVERAGE)
    target_compile_options(ccbuildlib PRIVATE ${CCBUILD_COVERAGE_COMPILE_FLAGS})
    target_link_options(ccbuildlib PRIVATE ${CCBUILD_COVERAGE_LINK_FLAGS})
endif ()

# ccbuild cli
add_executable(ccbuild src/main.cc)
target_link_libraries(ccbuild PRIVATE ccbuildlib)

target_compile_definitions(ccbuild PRIVATE
        CCBUILD_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
        CCBUILD_LIB_DIR="${CMAKE_BINARY_DIR}"
        CCBUILD_NINJA_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/ninja/src"
)

# When libraries are coverage-instrumented, the runner must also link with
# the profiler runtime, otherwise we get undefined ___llvm_profile_runtime.
if (CCBUILD_COVERAGE)
    if (CCBUILD_COVERAGE_CLANG)
        target_compile_definitions(ccbuild PRIVATE
                CCBUILD_COVERAGE_LINK_FLAGS="-fprofile-instr-generate")
    else ()
        target_compile_definitions(ccbuild PRIVATE
                CCBUILD_COVERAGE_LINK_FLAGS="--coverage")
    endif ()
endif ()

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
            tests/targets/executable_test.cc
            tests/targets/static_library_test.cc
            tests/internal/validators_test.cc
            tests/internal/compiler_test.cc
            tests/project/project_test.cc
    )

    target_link_libraries(ccbuild_tests PRIVATE ccbuildlib GTest::gtest_main)
    target_include_directories(ccbuild_tests PRIVATE src)
    if (CCBUILD_COVERAGE)
        target_compile_options(ccbuild_tests PRIVATE ${CCBUILD_COVERAGE_COMPILE_FLAGS})
        target_link_options(ccbuild_tests PRIVATE ${CCBUILD_COVERAGE_LINK_FLAGS})
    endif ()
    include(GoogleTest)
    gtest_discover_tests(ccbuild_tests)

    # coverage report
    if (CCBUILD_COVERAGE)
        if (CCBUILD_COVERAGE_CLANG)
            find_program(LLVM_PROFDATA llvm-profdata
                    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
            find_program(LLVM_COV llvm-cov
                    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
            if (LLVM_PROFDATA AND LLVM_COV)
                add_custom_target(coverage
                        COMMENT "Generating code coverage report (Clang)..."
                        COMMAND ${CMAKE_COMMAND} -E env
                        LLVM_PROFILE_FILE=ccbuild_tests.profraw
                        $<TARGET_FILE:ccbuild_tests>
                        COMMAND ${LLVM_PROFDATA} merge -sparse
                        ccbuild_tests.profraw -o ccbuild_tests.profdata
                        COMMAND ${LLVM_COV} show $<TARGET_FILE:ccbuild_tests>
                        -instr-profile=ccbuild_tests.profdata
                        -format=html
                        -output-dir=coverage-report
                        -show-line-counts-or-regions
                        -show-branches=count
                        -ignore-filename-regex='ninja/|_deps/|tests/|/usr/'
                        COMMAND ${LLVM_COV} report $<TARGET_FILE:ccbuild_tests>
                        -instr-profile=ccbuild_tests.profdata
                        -ignore-filename-regex='ninja/|_deps/|tests/|/usr/'
                        COMMAND ${CMAKE_COMMAND} -E echo
                        "Coverage report: ${CMAKE_BINARY_DIR}/coverage-report/index.html"
                        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                        DEPENDS ccbuild_tests
                )
            else ()
                message(WARNING "llvm-profdata/llvm-cov not found — 'coverage' target disabled."
                        " Install LLVM: brew install llvm")
            endif ()
        else ()
            find_program(LCOV lcov)
            find_program(GENHTML genhtml)
            if (LCOV AND GENHTML)
                add_custom_target(coverage
                        COMMENT "Generating code coverage report (GCC)..."
                        COMMAND ${LCOV} --directory . --zerocounters
                        COMMAND $<TARGET_FILE:ccbuild_tests>
                        COMMAND ${LCOV} --directory . --capture --output-file coverage.info
                        --rc branch_coverage=1 --ignore-errors mismatch
                        COMMAND ${LCOV} --remove coverage.info
                        '*/ninja/*' '*/tests/*' '/usr/*' '*/_deps/*'
                        --output-file coverage.filtered.info
                        --rc branch_coverage=1 --ignore-errors unused
                        COMMAND ${GENHTML} coverage.filtered.info
                        --output-directory coverage-report
                        --branch-coverage
                        --title "ccbuild coverage"
                        --ignore-errors mismatch
                        COMMAND ${CMAKE_COMMAND} -E echo
                        "Coverage report: ${CMAKE_BINARY_DIR}/coverage-report/index.html"
                        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                        DEPENDS ccbuild_tests
                )
            else ()
                message(WARNING "lcov/genhtml not found — 'coverage' target disabled. Install lcov.")
            endif ()
        endif ()
    endif ()
endif ()



