add_library(ps_loader_ulog STATIC UlogPlugin.cpp)
target_include_directories(ps_loader_ulog PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ps_loader_ulog PRIVATE ps_kernel)
