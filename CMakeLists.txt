cmake_minimum_required(VERSION 3.16)
project(ascii_rdb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
add_subdirectory(Parser)

option(BUILD_QT_RDB_VIEWER "Build the Qt 6 RDB TableView example" OFF)
if(BUILD_QT_RDB_VIEWER)
    add_subdirectory(examples/qt_rdb_viewer)
endif()
