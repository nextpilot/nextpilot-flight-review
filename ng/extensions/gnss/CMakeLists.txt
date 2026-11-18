find_package(Qt6 REQUIRED COMPONENTS Core)

# 卫导领域包：预留位，格式平移时在此目录追加驱动即可，不改其他代码。
add_library(ng_ext_gnss OBJECT
    GnssPack.cpp
    GnssPack.h
)
target_include_directories(ng_ext_gnss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_ext_gnss PUBLIC ng_core)
set_target_properties(ng_ext_gnss PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
