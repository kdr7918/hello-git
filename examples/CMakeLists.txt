add_executable(rdb-database-example rdb_database_example.cpp)
target_link_libraries(rdb-database-example PRIVATE rdb_indexed_file)
target_compile_options(rdb-database-example PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(BUILD_TESTING)
    rdb_add_test(
        rdb-database-example-smoke
        rdb-database-example
        ARGS "${CMAKE_CURRENT_SOURCE_DIR}/../standard_sample.rdb"
             "M1.SPACING.1"
    )
    set_tests_properties(rdb-database-example-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION
            "Traversed values: properties=6, vertices=4, edges=2"
    )

    rdb_add_test(
        rdb-database-example-tagged-keys
        rdb-database-example
        ARGS "${CMAKE_CURRENT_SOURCE_DIR}/../standard_sample.rdb"
             "M1.SPACING.1"
    )
    set_tests_properties(rdb-database-example-tagged-keys PROPERTIES
        PASS_REGULAR_EXPRESSION
            "Tagged keys: PP PA EL EW CN CN"
    )

    if(MINGW)
        get_filename_component(RDB_EXAMPLE_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    else()
        set(RDB_EXAMPLE_RUNTIME_DIR "")
    endif()
    add_test(
        NAME rdb-database-example-missing-check
        COMMAND "${CMAKE_COMMAND}"
            "-DEXAMPLE=$<TARGET_FILE:rdb-database-example>"
            "-DRDB_FILE=${CMAKE_CURRENT_SOURCE_DIR}/empty_checks_sample.rdb"
            "-DRUNTIME_DIR=${RDB_EXAMPLE_RUNTIME_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_missing_check.cmake"
    )
endif()
