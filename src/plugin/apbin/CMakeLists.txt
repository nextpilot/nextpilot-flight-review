add_library(ps_loader_apbin STATIC ApbinPlugin.cpp)
target_include_directories(ps_loader_apbin PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ps_loader_apbin PRIVATE ps_kernel)
