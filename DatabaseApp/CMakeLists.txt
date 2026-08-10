cmake_minimum_required(VERSION 3.16)
project(database_rdb_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 5.9 REQUIRED COMPONENTS Widgets)

set(DATABASE_APP_SOURCES
    calibre_text_dock.cpp
    calibre_text_dock.hpp
    calibre_text_dock.ui
    rdb_database.cpp
    rdb_database.hpp
    rdb_model.cpp
    rdb_model.hpp
    rdb_parser_workers.cpp
    rdb_parser_workers.hpp
    rdb_tree_model.cpp
    rdb_tree_model.hpp
)

add_executable(database-rdb-app
    main.cpp
    ${DATABASE_APP_SOURCES}
)

target_include_directories(database-rdb-app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Parser
)

target_link_libraries(database-rdb-app PRIVATE Qt5::Widgets)

target_compile_options(database-rdb-app PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

include(CTest)
if(BUILD_TESTING)
    add_executable(database-app-model-test
        database_app_model_test.cpp
        rdb_database.cpp
        rdb_database.hpp
        rdb_model.cpp
        rdb_model.hpp
        rdb_tree_model.cpp
        rdb_tree_model.hpp
    )
    target_include_directories(database-app-model-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../Parser
    )
    target_link_libraries(database-app-model-test PRIVATE Qt5::Widgets)
    target_compile_definitions(database-app-model-test PRIVATE
        RDB_SAMPLE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../standard_sample.rdb"
    )
    target_compile_options(database-app-model-test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    add_test(NAME database-app-model-test COMMAND database-app-model-test)

    add_executable(database-app-thread-test
        database_app_thread_test.cpp
        ${DATABASE_APP_SOURCES}
    )
    target_include_directories(database-app-thread-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../Parser
    )
    target_link_libraries(database-app-thread-test PRIVATE Qt5::Widgets)
    target_compile_definitions(database-app-thread-test PRIVATE
        RDB_SAMPLE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../standard_sample.rdb"
        RDB_SECOND_SAMPLE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../large_standard_sample.rdb"
    )
    target_compile_options(database-app-thread-test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    add_test(NAME database-app-thread-test COMMAND database-app-thread-test)

    add_executable(database-app-worker-test
        database_app_worker_test.cpp
        rdb_database.cpp
        rdb_database.hpp
        rdb_model.cpp
        rdb_model.hpp
        rdb_parser_workers.cpp
        rdb_parser_workers.hpp
        rdb_tree_model.cpp
        rdb_tree_model.hpp
    )
    target_include_directories(database-app-worker-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../Parser
    )
    target_link_libraries(database-app-worker-test PRIVATE Qt5::Widgets)
    target_compile_definitions(database-app-worker-test PRIVATE
        RDB_SAMPLE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../standard_sample.rdb"
    )
    target_compile_options(database-app-worker-test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    add_test(NAME database-app-worker-test COMMAND database-app-worker-test)
    if(NOT WIN32)
        set_tests_properties(
            database-app-model-test
            database-app-thread-test
            database-app-worker-test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()
endif()
