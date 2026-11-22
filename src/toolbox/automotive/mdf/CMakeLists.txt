find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(ZLIB REQUIRED)

# OBJECT 库：保证自注册 TU 一定参与链接（见 common/CMakeLists.txt 的说明）。
add_library(ps_parser_mdf OBJECT
    MdfParser.cpp
    MdfFormatParser.cpp
    MdfPack.cpp
    MdfPack.h
)
target_include_directories(ps_parser_mdf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_parser_mdf PUBLIC ps_context ZLIB::ZLIB)
set_target_properties(ps_parser_mdf PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
