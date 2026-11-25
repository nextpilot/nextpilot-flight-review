# ps_module：模块元数据契约库（纯契约，无运行时）。
# A1b 起 IModule/IUiContext 已更名为 IWorkbenchContribution 并迁往 libs/workbench；
# 本库暂只留 ModuleInfo（物理目录改名归 A3b）。未来外部插件（M3）链接本库取元数据。
add_library(ps_module STATIC
    include/ModuleInfo.h
)

target_include_directories(ps_module PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ps_module PUBLIC ps_context ps_dataset ps_intent ps_registry ps_foundation Qt6::Core)
set_target_properties(ps_module PROPERTIES AUTOMOC ON CXX_STANDARD 17)
