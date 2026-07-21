cmake_minimum_required(VERSION 3.10)
project(cpp11_fast_text_parser LANGUAGES CXX)

option(FASTTEXT_BUILD_TESTS "Build tests" ON)
option(FASTTEXT_BUILD_BENCHMARK "Build benchmark" ON)

add_library(fast_text_parser INTERFACE)
target_include_directories(fast_text_parser INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(fast_text_parser INTERFACE cxx_std_11)

add_executable(fast_text_parser_example examples/parse_file.cpp)
target_link_libraries(fast_text_parser_example PRIVATE fast_text_parser)

if(FASTTEXT_BUILD_TESTS)
    enable_testing()
    add_executable(fast_text_parser_tests tests/test_fast_text_parser.cpp)
    target_link_libraries(fast_text_parser_tests PRIVATE fast_text_parser)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(fast_text_parser_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME fast_text_parser_tests COMMAND fast_text_parser_tests)
endif()

if(FASTTEXT_BUILD_BENCHMARK)
    add_executable(fast_text_parser_benchmark benchmark/benchmark.cpp)
    target_link_libraries(fast_text_parser_benchmark PRIVATE fast_text_parser)
    if(FASTTEXT_BUILD_TESTS)
        add_test(NAME fast_text_parser_benchmark_empty
                 COMMAND fast_text_parser_benchmark
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/empty.txt)
    endif()
endif()
