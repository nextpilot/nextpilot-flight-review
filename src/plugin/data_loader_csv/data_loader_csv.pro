

TARGET = $$qtLibraryTarget(data_loader_csv)
TEMPLATE = lib

QT += core gui widgets xml

DESTDIR = $$OUT_PWD/lib

SOURCES += data_loader_csv.cpp

HEADERS += data_loader_csv.hpp


include($$PWD/../../common/common.pri)


win32-msvc* {
    QMAKE_CXXFLAGS *=  /wd"4100"
    contains (QMAKE_CXXFLAGS_WARN_ON, -w34100) : QMAKE_CXXFLAGS_WARN_ON -= -w34100
}

FORMS += \
    data_loader_csv.ui
