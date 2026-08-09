find_package(Qt5 5.9 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

add_executable(mainwindow-app WIN32
    calibre_text_dock_table.cpp
    calibre_text_dock_table.hpp
    main.cpp
    mainwindow.cpp
    mainwindow.hpp
    rdb_table_model.cpp
    rdb_table_model.hpp
    rdb_tree_model.cpp
    rdb_tree_model.hpp
)

target_link_libraries(mainwindow-app PRIVATE Qt5::Widgets Threads::Threads)

target_include_directories(mainwindow-app PRIVATE
    ${PROJECT_SOURCE_DIR}/Parser
)

target_compile_options(mainwindow-app PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(BUILD_TESTING)
    add_executable(rdb-ui-smoke-test
        calibre_text_dock_table.cpp
        calibre_text_dock_table.hpp
        rdb_table_model.cpp
        rdb_table_model.hpp
        rdb_tree_model.cpp
        rdb_tree_model.hpp
        rdb_ui_smoke_test.cpp
    )
    target_link_libraries(rdb-ui-smoke-test PRIVATE
        Qt5::Widgets
        Threads::Threads
    )
    target_include_directories(rdb-ui-smoke-test PRIVATE
        ${PROJECT_SOURCE_DIR}/Parser
    )
    target_compile_definitions(rdb-ui-smoke-test PRIVATE
        RDB_SAMPLE_FILE="${PROJECT_SOURCE_DIR}/standard_sample.rdb"
    )
    target_compile_options(rdb-ui-smoke-test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    if(WIN32)
        get_target_property(RDB_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
        get_filename_component(RDB_QT_BIN_DIR "${RDB_QMAKE_EXECUTABLE}" DIRECTORY)
        rdb_add_test(rdb-ui-smoke-test rdb-ui-smoke-test
            RUNTIME_DIRS "${RDB_QT_BIN_DIR}"
        )
    else()
        rdb_add_test(rdb-ui-smoke-test rdb-ui-smoke-test)
    endif()
    if(WIN32)
        set(RDB_TEST_QPA_PLATFORM windows)
    else()
        set(RDB_TEST_QPA_PLATFORM offscreen)
    endif()
    set_tests_properties(rdb-ui-smoke-test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=${RDB_TEST_QPA_PLATFORM}"
    )
endif()
