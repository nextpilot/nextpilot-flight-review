

TARGET = $$qtLibraryTarget(data_loader_ulog)
TEMPLATE = lib

QT += core gui widgets xml

DESTDIR = $$OUT_PWD/lib


SOURCES += data_loader_ulog.cpp

HEADERS += data_loader_ulog.hpp

include($$PWD/../../common/common.pri)
include($$PWD/../../3rdparty/ulog_cpp/ulog_cpp/ulog_cpp.pri)

win32-msvc* {
    QMAKE_CXXFLAGS *=  /wd"4100"
    contains (QMAKE_CXXFLAGS_WARN_ON, -w34100) : QMAKE_CXXFLAGS_WARN_ON -= -w34100
}
