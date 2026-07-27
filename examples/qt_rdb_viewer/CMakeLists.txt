# Qt 5.9 API만 사용한다. 개발 환경에 Qt 5가 없을 때만 Qt 6로 호환성 빌드한다.
find_package(Qt5 QUIET COMPONENTS Core Widgets Concurrent Test)
if(Qt5_FOUND)
    set(RDB_QT_PACKAGE Qt5)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent Test)
    set(RDB_QT_PACKAGE Qt6)
endif()

set(CMAKE_AUTOMOC ON)

add_executable(qt-rdb-viewer
    main.cpp
    rdb_viewer.cpp
    rdb_viewer.hpp
    rdb_viewer_models.cpp
    rdb_viewer_models.hpp
)
target_link_libraries(qt-rdb-viewer PRIVATE
    ascii_rdb_parser
    ${RDB_QT_PACKAGE}::Core
    ${RDB_QT_PACKAGE}::Widgets
    ${RDB_QT_PACKAGE}::Concurrent
)
target_compile_options(qt-rdb-viewer PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(BUILD_TESTING)
    add_executable(qt-rdb-viewer-tests
        rdb_viewer_models_test.cpp
        rdb_viewer_models.cpp
        rdb_viewer_models.hpp
    )
    target_link_libraries(qt-rdb-viewer-tests PRIVATE
        ascii_rdb_parser
        ${RDB_QT_PACKAGE}::Core
        ${RDB_QT_PACKAGE}::Widgets
        ${RDB_QT_PACKAGE}::Test
    )
    target_compile_options(qt-rdb-viewer-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME qt-rdb-viewer-tests COMMAND qt-rdb-viewer-tests)
    set_tests_properties(qt-rdb-viewer-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(qt-rdb-viewer-integration-tests
        rdb_viewer_integration_test.cpp
        rdb_viewer.cpp
        rdb_viewer.hpp
        rdb_viewer_models.cpp
        rdb_viewer_models.hpp
    )
    target_link_libraries(qt-rdb-viewer-integration-tests PRIVATE
        ascii_rdb_parser
        ${RDB_QT_PACKAGE}::Core
        ${RDB_QT_PACKAGE}::Widgets
        ${RDB_QT_PACKAGE}::Concurrent
        ${RDB_QT_PACKAGE}::Test
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
