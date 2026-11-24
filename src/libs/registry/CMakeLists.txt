# ps_registry：泛型注册表最小核 + 工具箱贡献契约/容器。
# 泛型 Registry<T> / IndexedByKind<T> / 自注册助手（header-only）+ 贡献接口与容器（STATIC）。
# 只依赖 Qt Core + STL + foundation(Result)，headless 可用。
add_library(ps_registry STATIC
    src/Domain.cpp
    src/ToolboxHub.cpp
    src/UiSlotHub.cpp
    src/Region.cpp
    src/RegistryNode.cpp
    src/RegistryTree.cpp
    include/Domain.h
    include/Contribution.h
    include/ProductSpec.h
    include/Region.h
    include/ToolboxHub.h
    include/UiSlotHub.h
    include/PanelFactoryHub.h
    include/RegistryNode.h
    include/RegistryTree.h
)
target_include_directories(ps_registry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# ToolboxHub 用 IMessageParser(parser) 完整类型、PanelFactory 用 IIntentExecutor(intent)。
# registry↔parser 头文件互依赖（IToolContribution 基类 / IMessageParser 视图），CMake 静态库自动处理。
target_link_libraries(ps_registry PUBLIC Qt6::Core ps_foundation ps_parser ps_intent)
set_target_properties(ps_registry PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
