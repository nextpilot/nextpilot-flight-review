add_library(ps_loader_common STATIC PsLoaderCommon.cpp CanFrames.cpp)

target_include_directories(ps_loader_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src/include
)

target_link_libraries(ps_loader_common PUBLIC ps_kernel Qt6::Core)
