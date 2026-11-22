find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(ZLIB REQUIRED)

# OBJECT 库：保证自注册 TU 一定参与链接（见 common/CMakeLists.txt 的说明）。
add_library(ps_parser_blf OBJECT
    BlfParser.cpp
    BlfFormatParser.cpp
    BlfPack.cpp
    BlfPack.h
)
target_include_directories(ps_parser_blf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_parser_blf PUBLIC ps_context ZLIB::ZLIB)
set_target_properties(ps_parser_blf PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
