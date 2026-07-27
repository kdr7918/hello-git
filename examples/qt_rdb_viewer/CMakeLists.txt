find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent Test)
set(CMAKE_AUTOMOC ON)

qt_add_executable(qt-rdb-viewer
    main.cpp
    rdb_viewer.cpp
    rdb_viewer.hpp
    rdb_viewer_models.cpp
    rdb_viewer_models.hpp
)
target_link_libraries(qt-rdb-viewer PRIVATE
    ascii_rdb_parser Qt6::Core Qt6::Widgets Qt6::Concurrent
)
target_compile_options(qt-rdb-viewer PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(BUILD_TESTING)
    qt_add_executable(qt-rdb-viewer-tests
        rdb_viewer_models_test.cpp
        rdb_viewer_models.cpp
        rdb_viewer_models.hpp
    )
    target_link_libraries(qt-rdb-viewer-tests PRIVATE
        ascii_rdb_parser Qt6::Core Qt6::Widgets Qt6::Test
    )
    target_compile_options(qt-rdb-viewer-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME qt-rdb-viewer-tests COMMAND qt-rdb-viewer-tests)
    set_tests_properties(qt-rdb-viewer-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(qt-rdb-viewer-integration-tests
        rdb_viewer_integration_test.cpp
        rdb_viewer.cpp
        rdb_viewer.hpp
        rdb_viewer_models.cpp
        rdb_viewer_models.hpp
    )
    target_link_libraries(qt-rdb-viewer-integration-tests PRIVATE
        ascii_rdb_parser Qt6::Core Qt6::Widgets Qt6::Concurrent Qt6::Test
    )
    target_compile_definitions(qt-rdb-viewer-integration-tests PRIVATE
        RDB_SAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../.."
    )
    target_compile_options(qt-rdb-viewer-integration-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME qt-rdb-viewer-integration-tests COMMAND qt-rdb-viewer-integration-tests)
    set_tests_properties(qt-rdb-viewer-integration-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
