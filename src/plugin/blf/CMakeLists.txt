add_library(ps_loader_blf STATIC BlfPlugin.cpp)
target_include_directories(ps_loader_blf PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ps_loader_blf PRIVATE ps_kernel)
