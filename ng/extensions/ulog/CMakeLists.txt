find_package(Qt6 REQUIRED COMPONENTS Core)
add_library(ng_ext_ulog STATIC
    UlogExtension.cpp
    UlogStreamParser.cpp
)
target_include_directories(ng_ext_ulog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_ext_ulog PUBLIC ng_host)
set_target_properties(ng_ext_ulog PROPERTIES AUTOMOC ON CXX_STANDARD 17)
