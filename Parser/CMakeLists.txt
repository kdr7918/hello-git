add_library(ascii_rdb_parser
    ascii_rdb_parser.cpp
)
target_include_directories(ascii_rdb_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(ascii_rdb_parser PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(BUILD_TESTING)
    add_executable(rdb-parser-tests rdb_parser_tests.cpp)
    target_link_libraries(rdb-parser-tests PRIVATE ascii_rdb_parser)
    target_compile_options(rdb-parser-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    target_compile_definitions(rdb-parser-tests PRIVATE RDB_SAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME rdb-parser-tests COMMAND rdb-parser-tests)

    add_executable(rdb-check-index-header-test rdb_check_index_header_test.cpp)
    target_compile_options(rdb-check-index-header-test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME rdb-check-index-header-test COMMAND rdb-check-index-header-test)
endif()
