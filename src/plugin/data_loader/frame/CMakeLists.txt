set(FRAME_COMPONENT_DIR ${PROJECT_SOURCE_DIR}/src/components/protocol_frame)

add_library(ps_data_loader_frame STATIC
  ${FRAME_COMPONENT_DIR}/FrameDataLoader.cpp
  ${FRAME_COMPONENT_DIR}/SequentialCodec.cpp
)
target_include_directories(ps_data_loader_frame PRIVATE
  ${PROJECT_SOURCE_DIR}/src/plugin/include
  ${PROJECT_SOURCE_DIR}/src/plugin/sdk/cpp
  ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/common
  ${FRAME_COMPONENT_DIR}
)
target_link_libraries(ps_data_loader_frame PRIVATE ps_loader_common ps_protocol)
