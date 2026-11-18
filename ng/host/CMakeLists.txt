find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(ng_host STATIC
    src/ModuleHost.cpp
    src/DataLoadHandler.cpp
    src/DataCommandsHandler.cpp
    src/BuiltinModuleRegistry.cpp
    include/ModuleInfo.h
    include/IModule.h
    include/ModuleHost.h
    include/DataLoadHandler.h
    include/DataCommandsHandler.h
    include/BuiltinModuleRegistry.h
)

target_include_directories(ng_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_host PUBLIC ng_core Qt6::Core)
set_target_properties(ng_host PROPERTIES AUTOMOC ON CXX_STANDARD 17)
