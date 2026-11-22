find_package(Qt6 REQUIRED COMPONENTS Core)

# zlib 走顶层 PsDependencies 解析出的 ps_zlib（系统/vcpkg/源码构建三选一）。
# 单独把本目录 add_subdirectory 进来时（不常见）退回 find_package。
if(TARGET ps_zlib)
    set(PS_MDF_ZLIB ps_zlib)
else()
    find_package(ZLIB REQUIRED)
    set(PS_MDF_ZLIB ZLIB::ZLIB)
endif()

# OBJECT 库：保证自注册 TU 一定参与链接（见 common/CMakeLists.txt 的说明）。
add_library(ps_parser_mdf OBJECT
    MdfParser.cpp
    MdfFormatParser.cpp
    MdfPack.cpp
    MdfPack.h
)
target_include_directories(ps_parser_mdf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_parser_mdf PUBLIC ps_context ${PS_MDF_ZLIB})
set_target_properties(ps_parser_mdf PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
