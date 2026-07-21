cmake_minimum_required(VERSION 3.16)
project(calibre_ascii_rdb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(calibre_ascii_rdb_parser
    calibre_ascii_rdb_parser.cpp
)
target_include_directories(calibre_ascii_rdb_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(calibre_ascii_rdb_parser PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

include(CTest)
if(BUILD_TESTING)
    add_executable(rdb-parser-tests rdb_parser_tests.cpp)
    target_link_libraries(rdb-parser-tests PRIVATE calibre_ascii_rdb_parser)
    target_compile_definitions(rdb-parser-tests PRIVATE RDB_SAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rdb-parser-tests COMMAND rdb-parser-tests)
endif()
