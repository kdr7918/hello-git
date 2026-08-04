add_executable(rdb-indexed-example rdb_indexed_example.cpp)
target_link_libraries(rdb-indexed-example PRIVATE ascii_rdb_parser)
target_compile_options(rdb-indexed-example PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(BUILD_TESTING)
    add_test(
        NAME rdb-indexed-example-smoke
        COMMAND rdb-indexed-example
            "${CMAKE_CURRENT_SOURCE_DIR}/../standard_sample.rdb"
            "M1.SPACING.1"
    )
    set_tests_properties(rdb-indexed-example-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION
            "Traversed values: properties=5, vertices=4, edges=2"
    )

    add_test(
        NAME rdb-indexed-example-missing-check
        COMMAND "${CMAKE_COMMAND}"
            "-DEXAMPLE=$<TARGET_FILE:rdb-indexed-example>"
            "-DRDB_FILE=${CMAKE_CURRENT_SOURCE_DIR}/empty_checks_sample.rdb"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_missing_check.cmake"
    )
endif()
