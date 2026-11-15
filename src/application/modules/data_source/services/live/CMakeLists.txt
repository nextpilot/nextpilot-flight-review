add_library(ps_live STATIC
  LiveSession.h
  LiveSession.cpp
  PsCaptureFormat.h
  PsCaptureFormat.cpp
  record/StreamRecorder.h
  record/StreamRecorder.cpp
  record/McapWriter.h
  record/McapWriter.cpp
)
target_include_directories(ps_live PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(ps_live PUBLIC Qt6::Core ps_kernel)
