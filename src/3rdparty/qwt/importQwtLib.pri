
QWT_LIB_PATH = $$clean_path($$shadowed($$PWD)/lib)

CONFIG(debug, debug|release) {
    LIBS += -L$$QWT_LIB_PATH -lqwtd
} else {
    LIBS += -L$$QWT_LIB_PATH -lqwt
}

INCLUDEPATH += $$PWD/src
DEPENDPATH += $$PWD/src
