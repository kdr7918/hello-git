add_library(rdb_indexed_file
    rdb_indexed_file.cpp
)
target_include_directories(rdb_indexed_file PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(rdb_indexed_file PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(BUILD_TESTING)
    add_executable(rdb-parser-tests rdb_parser_tests.cpp)
    target_link_libraries(rdb-parser-tests PRIVATE rdb_indexed_file)
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

    add_executable(rdb-unified-database-test rdb_unified_database_test.cpp)
    target_link_libraries(rdb-unified-database-test PRIVATE rdb_indexed_file)
    target_compile_options(rdb-unified-database-test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    target_compile_definitions(rdb-unified-database-test PRIVATE RDB_SAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME rdb-unified-database-test COMMAND rdb-unified-database-test)
endif()
