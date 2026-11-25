find_package(Qt6 REQUIRED COMPONENTS Core)

# zlib 走顶层 Dependencies 解析出的 ps_zlib（系统 / PS_DEPS_ROOT / FetchContent
# 源码构建三选一），Windows 无系统 zlib 时由 FetchContent 兜底。
# 单独把本目录 add_subdirectory 进来时（不常见）退回 find_package。
if(TARGET ps_zlib)
    set(PS_BLF_ZLIB ps_zlib)
else()
    find_package(ZLIB REQUIRED)
    set(PS_BLF_ZLIB ZLIB::ZLIB)
endif()

# OBJECT 库：保证自注册 TU 一定参与链接（见 common/CMakeLists.txt 的说明）。
add_library(ps_parser_blf OBJECT
    BlfParser.cpp
    BlfFormatParser.cpp
    BlfPack.cpp
    BlfPack.h
)
target_include_directories(ps_parser_blf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_parser_blf PUBLIC ps_context ${PS_BLF_ZLIB})
set_target_properties(ps_parser_blf PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
