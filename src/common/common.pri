QT += xml

HEADERS += $$PWD/data_source_base.hpp

# add qwt lib
LIBS += -L$$PWD/../3rdparty/qwt/lib -lqwtd
LIBS += -L$$PWD/../3rdparty/qwt/lib -lqwt

INCLUDEPATH += $$PWD/../3rdparty/qwt/src
INCLUDEPATH += $$PWD
