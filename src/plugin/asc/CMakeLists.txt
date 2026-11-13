add_library(ps_loader_asc STATIC AscPlugin.cpp)
target_include_directories(ps_loader_asc PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ps_loader_asc PRIVATE ps_kernel)
