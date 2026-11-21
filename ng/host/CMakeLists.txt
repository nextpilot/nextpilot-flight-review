find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_library(ng_host STATIC
    src/RuntimeHost.cpp
    src/DataLoadHandler.cpp
    src/DataCommandsHandler.cpp
    src/XlsxBook.cpp
    src/AIToolsHandler.cpp
    src/SkeletonSlots.cpp
    src/BuiltinModuleRegistry.cpp
    include/ModuleInfo.h
    include/IModule.h
    include/RuntimeHost.h
    include/DataLoadHandler.h
    include/DataCommandsHandler.h
    include/XlsxBook.h
    include/AIToolsHandler.h
    include/SkeletonSlots.h
    include/BuiltinModuleRegistry.h
)

target_include_directories(ng_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# 录制引擎（Recorder）：data.record.* 意图要直接创建它，故 host 公开链接。
target_link_libraries(ng_host PUBLIC ng_context ng_registry ng_recording Qt6::Core Qt6::Network)
set_target_properties(ng_host PROPERTIES AUTOMOC ON CXX_STANDARD 17)
