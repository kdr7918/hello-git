add_executable(rdb-database-example rdb_database_example.cpp)
target_link_libraries(rdb-database-example PRIVATE rdb_indexed_file)
target_compile_options(rdb-database-example PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(BUILD_TESTING)
    add_test(
        NAME rdb-database-example-smoke
        COMMAND rdb-database-example
            "${CMAKE_CURRENT_SOURCE_DIR}/../standard_sample.rdb"
            "M1.SPACING.1"
    )
    set_tests_properties(rdb-database-example-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION
            "Traversed values: properties=5, vertices=4, edges=2"
    )

    add_test(
        NAME rdb-database-example-missing-check
        COMMAND "${CMAKE_COMMAND}"
            "-DEXAMPLE=$<TARGET_FILE:rdb-database-example>"
            "-DRDB_FILE=${CMAKE_CURRENT_SOURCE_DIR}/empty_checks_sample.rdb"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_missing_check.cmake"
    )
endif()
