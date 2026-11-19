find_package(Qt6 REQUIRED COMPONENTS Core Network)

# 录制层：格式（成帧）× 后端（输出）的正交组合。
# 格式侧只管字节长什么样，后端侧只管字节送到哪。
add_library(ng_recording STATIC
    RawCodec.cpp
    RawCodec.h
    McapCodec.cpp
    McapCodec.h
    Sink.cpp
    Sink.h
    Recorder.cpp
    Recorder.h
    RecordingFormat.h
    RecordingFormat.cpp
    RecordingBackend.h
    RecordingBackend.cpp
)
target_include_directories(ng_recording PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_recording PUBLIC ng_core Qt6::Core Qt6::Network)
set_target_properties(ng_recording PROPERTIES AUTOMOC ON CXX_STANDARD 17)
