find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_library(ps_host STATIC
    src/RuntimeHost.cpp
    src/DataLoadHandler.cpp
    src/DataCommandsHandler.cpp
    src/XlsxBook.cpp
    src/AIToolsHandler.cpp
    src/ShellContributions.cpp
    src/StaticRegistryNodes.cpp
    src/Toolbox.cpp
    src/PluginScanner.cpp
    include/RuntimeHost.h
    include/DataLoadHandler.h
    include/DataCommandsHandler.h
    include/XlsxBook.h
    include/AIToolsHandler.h
    include/ShellContributions.h
    include/BuiltinModuleHub.h
    include/StaticRegistryNodes.h
    include/Toolbox.h
    include/PluginScanner.h
)

target_include_directories(ps_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# 录制引擎（Recorder）：data.record.* 意图要直接创建它，故 host 公开链接。
# 器官契约（IModule/ModuleInfo）已迁 libs/module：契约在 libs，host 只做运行时。
target_link_libraries(ps_host PUBLIC ps_module ps_workbench ps_context ps_registry ps_recording Qt6::Core Qt6::Network)
set_target_properties(ps_host PROPERTIES AUTOMOC ON CXX_STANDARD 17)
