find_package(Qt6 REQUIRED COMPONENTS Core)

# OBJECT 库而非 STATIC：自注册依赖 TU 里的静态对象构造函数，
# 静态库中未被引用的目标文件会被链接器丢弃，导致注册不生效。
# OBJECT 库的所有目标文件必然参与链接，注册一定发生。
add_library(ps_toolbox_common OBJECT
    CsvFormatParser.cpp
    CsvFormatParser.h
    CsvImportProbe.cpp
    CsvImportProbe.h
    CsvParser.cpp
    CsvParser.h
    CustomProtocolFormatParser.cpp
    CustomProtocolFormatParser.h
    CustomProtocolParser.cpp
    CustomProtocolParser.h
    CommonPack.cpp
    CommonPack.h
    PspxParser.cpp
    PspxParser.h
)
target_include_directories(ps_toolbox_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_toolbox_common PUBLIC ps_context ps_protocol)
set_target_properties(ps_toolbox_common PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
