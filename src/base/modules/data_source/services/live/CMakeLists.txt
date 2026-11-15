add_library(ps_live STATIC
  LiveSession.h
  LiveSession.cpp
  record/StreamRecorder.h
  record/StreamRecorder.cpp
  record/McapWriter.h
  record/McapWriter.cpp
)
target_include_directories(ps_live PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/common
)
target_link_libraries(ps_live PUBLIC Qt6::Core ps_kernel ps_loader_common)
