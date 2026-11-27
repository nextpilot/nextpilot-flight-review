# ps_context：进程级运行态容器（唯一实例在 app 组合根创建）。
# AppContext + SettingsStore + WorkspaceDataConfig + 摄取管道 + 交互注册表 + 脚本门面。
# 各类型定义在此，实例生命周期归上层；符合 libs 铁律（无全局活实例/服务定位器）。
add_library(ps_context STATIC
    src/AppContext.cpp
    src/InteractionHub.cpp
    src/SettingsStore.cpp
    src/WorkspaceDataConfig.cpp
    src/ChannelTransformConfig.cpp
    src/ScriptFacade.cpp
    src/DerivedSeriesHub.cpp
    src/IngestPipeline.cpp
    src/IngestOrchestrator.cpp
    src/ParserCatalog.cpp
    include/ParserCatalog.h
    src/MessageLog.cpp
    src/PersistentStateHub.cpp
    src/StatusHub.cpp
    src/WorkspacePreset.cpp
    include/AppContext.h
    include/InteractionHub.h
    include/SettingsStore.h
    include/WorkspaceDataConfig.h
    include/ChannelTransformConfig.h
    include/ScriptFacade.h
    include/DerivedSeriesHub.h
    include/IngestPipeline.h
    include/IngestOrchestrator.h
    include/MessageLog.h
    include/StatusHub.h
)
target_include_directories(ps_context PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ps_context PUBLIC
    Qt6::Core ps_kernel ps_foundation ps_dataset ps_registry ps_intent ps_uischema ps_parser)
set_target_properties(ps_context PROPERTIES AUTOMOC ON CXX_STANDARD 17)
