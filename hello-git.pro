QT += core gui widgets

CONFIG += c++11
CONFIG -= app_bundle

TEMPLATE = app
TARGET = large-file-viewer

INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/main_window.cpp \
    src/fast_text_reader.cpp \
    src/parser.cpp \
    src/workers.cpp \
    src/toc_tree_model.cpp \
    src/detail_table_model.cpp

HEADERS += \
    src/data_types.h \
    src/fast_text_reader.h \
    src/main_window.h \
    src/parser.h \
    src/workers.h \
    src/toc_tree_model.h \
    src/detail_table_model.h
