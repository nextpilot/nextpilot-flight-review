# ps_module：器官契约库（纯契约，无运行时）。
# IModule / ModuleInfo / UiContext / ModuleContext——模块与宿主之间的接口面。
# 运行时（RuntimeHost 的装载与 ModuleContext 方法体）在 host；契约在这里，
# 与 IIntentExecutor 居 libs/intent 同一先例。未来外部插件（M3）只需链接本库。
add_library(ps_module STATIC
    include/IModule.h
    include/ModuleInfo.h
)

target_include_directories(ps_module PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ps_module PUBLIC ps_context ps_dataset ps_intent ps_registry ps_foundation Qt6::Core)
set_target_properties(ps_module PROPERTIES AUTOMOC ON CXX_STANDARD 17)
