cmake_minimum_required(VERSION 3.5)
project(large_file_viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)

add_executable(large-file-viewer
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    src/parser.cpp
    src/parser.h
    src/workers.cpp
    src/workers.h
    src/toc_tree_model.cpp
    src/toc_tree_model.h
    src/detail_table_model.cpp
    src/detail_table_model.h
    src/data_types.h
)

target_include_directories(large-file-viewer PRIVATE src)
target_link_libraries(large-file-viewer PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
)
