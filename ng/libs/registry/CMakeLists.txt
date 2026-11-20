# ng_registry：泛型注册表最小核 + 工具箱贡献契约/容器。
# 泛型 Registry<T> / IndexedByKind<T> / 自注册助手（header-only）+ 贡献接口与容器（STATIC）。
# 只依赖 Qt Core + STL + foundation(Result)，headless 可用。
add_library(ng_registry STATIC
    src/Domain.cpp
    src/ContributionKind.cpp
    src/BuiltinContributions.cpp
    src/ExtensionRegistry.cpp
    src/UiSlotRegistry.cpp
    src/PanelFactoryRegistry.cpp
    include/Domain.h
    include/ContributionKind.h
    include/IToolContribution.h
    include/IToolPack.h
    include/IUiContribution.h
    include/IPanelContribution.h
    include/IScriptContribution.h
    include/IDataContribution.h
    include/IExampleContribution.h
    include/IDocContribution.h
    include/BuiltinContributions.h
    include/ExtensionRegistry.h
    include/UiSlotRegistry.h
    include/PanelFactoryRegistry.h
)
target_include_directories(ng_registry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# ExtensionRegistry 用 IMessageParser(parser) 完整类型、PanelFactory 用 IIntentExecutor(intent)。
# registry↔parser 头文件互依赖（IToolContribution 基类 / IMessageParser 视图），CMake 静态库自动处理。
target_link_libraries(ng_registry PUBLIC Qt6::Core ng_foundation ng_parser ng_intent)
set_target_properties(ng_registry PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
