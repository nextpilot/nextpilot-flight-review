find_package(Qt6 REQUIRED COMPONENTS Core)

# 卫导领域包：预留位，格式平移时在此目录追加驱动即可，不改其他代码。
add_library(ps_toolbox_gnss OBJECT
    GnssPack.cpp
    GnssPack.h
    NmeaMessageParser.cpp
    NmeaMessageParser.h
)
target_include_directories(ps_toolbox_gnss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_toolbox_gnss PUBLIC ps_context)
set_target_properties(ps_toolbox_gnss PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
