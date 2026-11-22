find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(SQLite3 REQUIRED)

# OBJECT 库：保证自注册 TU 一定参与链接（见 common/CMakeLists.txt 的说明）。
add_library(ps_parser_rosbag2 OBJECT
    Rosbag2Parser.cpp
    Rosbag2FormatParser.cpp
    Rosbag2Pack.cpp
    Rosbag2Pack.h
)
target_include_directories(ps_parser_rosbag2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_parser_rosbag2 PUBLIC ps_context SQLite::SQLite3)
set_target_properties(ps_parser_rosbag2 PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
