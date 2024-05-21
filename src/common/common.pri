QT += xml

HEADERS += $$PWD/data_source_base.hpp

# add qwt lib
QWT_LIB_PATH = $$clean_path($$shadowed($$PWD)/../3rdparty/qwt/lib)
CONFIG(debug, debug|release) {
    LIBS += -L$$QWT_LIB_PATH -lqwtd
} else {
    LIBS += -L$$QWT_LIB_PATH -lqwt
}

INCLUDEPATH += $$PWD/../../3rdparty/qwt/src
INCLUDEPATH += $$PWD
