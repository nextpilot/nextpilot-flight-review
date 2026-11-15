add_library(ps_loader_common STATIC PsLoaderCommon.cpp CanFrames.cpp Dbc.cpp)

target_include_directories(ps_loader_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src/base/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/base/kernel/include
)

target_link_libraries(ps_loader_common PUBLIC Qt6::Core)
