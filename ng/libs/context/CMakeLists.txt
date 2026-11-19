# ng_context：进程级运行态容器（唯一实例在 app 组合根创建）。
# AppContext + SettingsStore + WorkspaceDataConfig + 摄取管道 + 交互注册表 + 脚本门面。
# 各类型定义在此，实例生命周期归上层；符合 libs 铁律（无全局活实例/服务定位器）。
add_library(ng_context STATIC
    src/AppContext.cpp
    src/InteractionRegistry.cpp
    src/SettingsStore.cpp
    src/WorkspaceDataConfig.cpp
    src/ScriptFacade.cpp
    src/IngestPipeline.cpp
    src/IngestOrchestrator.cpp
    include/AppContext.h
    include/InteractionRegistry.h
    include/SettingsStore.h
    include/WorkspaceDataConfig.h
    include/ScriptFacade.h
    include/IngestPipeline.h
    include/IngestOrchestrator.h
)
target_include_directories(ng_context PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_context PUBLIC
    Qt6::Core ng_kernel ng_foundation ng_dataset ng_registry ng_intent ng_uischema ng_parser)
set_target_properties(ng_context PROPERTIES AUTOMOC ON CXX_STANDARD 17)
