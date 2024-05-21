include(ads.pri)

ADS_LIB_PATH = $$clean_path($$shadowed($$PWD)/lib)
LIBS += -L$$ADS_LIB_PATH

INCLUDEPATH += $$PWD/src
DEPENDPATH += $$PWD/src
