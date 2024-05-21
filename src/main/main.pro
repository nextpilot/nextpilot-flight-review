QT       += core gui widgets xml

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
CONFIG += debug_and_release

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

TARGET = Nextpilot-Flight-Review
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
   mainwindow.h

FORMS += \
    mainwindow.ui

RESOURCES += \
    resources.qrc

TRANSLATIONS += \
    $$PWD/../language/nextpilot-flight-review_zh_CN.ts

CONFIG += lrelease
CONFIG += embed_translations

# add ads lib
#include($$PWD/../3rdparty/Qt-Advanced-Docking-System/importAdsLib.pri)

# add qwt lib
#include($$PWD/../3rdparty/qwt/importQwtLib.pri)

# add SARibbon lib
include($$PWD/../3rdparty/SARibbon/importSARibbonBarLib.pri)

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
