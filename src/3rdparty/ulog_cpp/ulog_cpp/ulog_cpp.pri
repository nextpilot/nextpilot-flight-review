CONFIG += c++17

#SOURCES += $$files($$PWD/*.c)

SOURCES += $$PWD/data_container.cpp \
           $$PWD/messages.cpp       \
           $$PWD/reader.cpp

HEADERS += $$PWD/data_container.hpp         \
           $$PWD/data_handler_interface.hpp \
           $$PWD/exception.hpp              \
           $$PWD/messages.hpp               \
           $$PWD/raw_messages.hpp           \
           $$PWD/reader.hpp                 \
           $$PWD/subscription.hpp           \
           $$PWD/utils.hpp

INCLUDEPATH += $$PWD/..
