cmake_minimum_required(VERSION 3.16)
project(ascii_rdb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

function(rdb_add_test test_name target_name)
    cmake_parse_arguments(RDB_TEST "" "" "RUNTIME_DIRS;ARGS" ${ARGN})
    if(WIN32)
        set(rdb_test_path_entries ${RDB_TEST_RUNTIME_DIRS})
        if(MINGW)
            get_filename_component(rdb_compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
            list(APPEND rdb_test_path_entries "${rdb_compiler_bin_dir}")
        endif()
        list(APPEND rdb_test_path_entries "$ENV{PATH}")
        list(JOIN rdb_test_path_entries ";" rdb_test_path)
        add_test(NAME ${test_name} COMMAND
            "${CMAKE_COMMAND}" -E env "PATH=${rdb_test_path}"
            "$<TARGET_FILE:${target_name}>" ${RDB_TEST_ARGS}
        )
    else()
        add_test(NAME ${test_name} COMMAND
            "$<TARGET_FILE:${target_name}>" ${RDB_TEST_ARGS}
        )
    endif()
endfunction()

add_subdirectory(Parser)
add_subdirectory(examples)

option(BUILD_MAINWINDOW_APP "Build the Qt 5.9 MainWindow application" ON)
if(BUILD_MAINWINDOW_APP)
    add_subdirectory(app)
endif()
