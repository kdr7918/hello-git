cmake_minimum_required(VERSION 3.16)
project(original_rdb_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 5.9 REQUIRED COMPONENTS Widgets)

add_executable(original-rdb-app
    calibre_text_dock.cpp
    calibre_text_dock.hpp
    calibre_text_dock.ui
    main.cpp
    rdb_model.cpp
    rdb_model.hpp
    rdb_parser_workers.cpp
    rdb_parser_workers.hpp
    rdb_tree_model.cpp
    rdb_tree_model.hpp
    rdb_types.cpp
    rdb_types.hpp
)

target_include_directories(original-rdb-app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Parser
)

target_link_libraries(original-rdb-app PRIVATE Qt5::Widgets)

target_compile_options(original-rdb-app PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)
