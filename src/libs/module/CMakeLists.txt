# ps_module：模块元数据契约库（纯契约，无运行时）。
# A1b 起 IModule/IUiContext 已更名为 IWorkbenchContribution 并迁往 libs/workbench；
# 本库暂只留 ModuleInfo（物理目录改名归 A3b）。未来外部插件（M3）链接本库取元数据。
#
# 纯契约 = 零编译单元（ModuleInfo 是普通 struct，不含 Q_OBJECT，没有可 moc 的内容），
# 所以必须是 INTERFACE 库，不能声明 STATIC（IKY124）：macOS 空归档中断、MSVC LNK1181、
# Linux 空归档假绿。原 AUTOMOC ON 也随之失去意义（INTERFACE 库不编译）。
add_library(ps_module INTERFACE)

target_include_directories(ps_module INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# INTERFACE 库只允许 INTERFACE 属性，依赖关键字必须用 INTERFACE。
target_link_libraries(ps_module INTERFACE
    ps_context ps_dataset ps_intent ps_registry ps_foundation Qt6::Core)
# C++17 要求由 compile_features 透传（INTERFACE 库上设 CXX_STANDARD 会被静默忽略）。
target_compile_features(ps_module INTERFACE cxx_std_17)
