QT += xml

HEADERS += $$PWD/data_source_base.hpp

INCLUDEPATH += $$PWD

# add qwt lib
include($$PWD/../3rdparty/qwt/importQwtLib.pri)

