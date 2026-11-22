find_package(Qt6 REQUIRED COMPONENTS Core)

# sqlite3 走顶层 PsDependencies 解析出的 ps_sqlite3（系统/vcpkg/源码构建三选一）。
# 单独把本目录 add_subdirectory 进来时（不常见）退回 find_package。
if(TARGET ps_sqlite3)
    set(PS_ROSBAG2_SQLITE ps_sqlite3)
else()
    find_package(SQLite3 REQUIRED)
    set(PS_ROSBAG2_SQLITE SQLite::SQLite3)
endif()

# OBJECT 库：保证自注册 TU 一定参与链接（见 common/CMakeLists.txt 的说明）。
add_library(ps_parser_rosbag2 OBJECT
    Rosbag2Parser.cpp
    Rosbag2FormatParser.cpp
    Rosbag2Pack.cpp
    Rosbag2Pack.h
)
target_include_directories(ps_parser_rosbag2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_parser_rosbag2 PUBLIC ps_context ${PS_ROSBAG2_SQLITE})
set_target_properties(ps_parser_rosbag2 PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
