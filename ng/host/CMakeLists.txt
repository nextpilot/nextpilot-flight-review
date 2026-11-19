find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(ng_host STATIC
    src/RuntimeHost.cpp
    src/DataLoadHandler.cpp
    src/DataCommandsHandler.cpp
    src/SkeletonSlots.cpp
    src/BuiltinModuleRegistry.cpp
    include/ModuleInfo.h
    include/IModule.h
    include/RuntimeHost.h
    include/DataLoadHandler.h
    include/DataCommandsHandler.h
    include/SkeletonSlots.h
    include/BuiltinModuleRegistry.h
)

target_include_directories(ng_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_host PUBLIC ng_context Qt6::Core)
set_target_properties(ng_host PROPERTIES AUTOMOC ON CXX_STANDARD 17)
