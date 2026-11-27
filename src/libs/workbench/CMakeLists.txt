# ps_workbench：工作台契约库（VSCode workbench 对位）。
# 放 access 接口、vscode.* 服务接口、IWorkbenchContribution、Event 支撑类型。
# 纯契约：只依赖 libs 地基（dataset/intent/recording/instantiation）+ Qt Core，
# 不含任何具体 Hub 类型、不依赖 context/host/gui，headless 可用。
#
# 纯契约 = 零编译单元，所以必须是 INTERFACE 库，不能声明 STATIC（IKY124）：
#   macOS（BSD ar）→ "ar: no archive members specified"，构建中断；
#   MSVC          → 不产出 .lib，链接 exe 时 LNK1181；
#   Linux（GNU ar）→ 生成空归档"假绿"，库在但什么都不提供。
# 头文件清单不再逐条列为源（INTERFACE 库不接受源），include 目录照样透传给消费者。
add_library(ps_workbench INTERFACE)

target_include_directories(ps_workbench INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# ps_context：IWorkspace::derived/configuration 与 IWindow::state 返回的 libs 具体类型，
# NullWindow 持有 PersistentStateHub；context 不反向依赖 workbench，无环。
# INTERFACE 库只允许 INTERFACE 属性，依赖关键字必须用 INTERFACE（写 PUBLIC 会报
# "INTERFACE library can only have INTERFACE properties"）。
target_link_libraries(ps_workbench INTERFACE
    ps_instantiation ps_dataset ps_intent ps_recording ps_context ps_module ps_registry ps_foundation Qt6::Core)
# 不编译就没有 CXX_STANDARD 可设（INTERFACE 库上 set_target_properties 的 CXX_STANDARD
# 会被静默忽略），C++17 要求改由 compile_features 透传给消费者。
target_compile_features(ps_workbench INTERFACE cxx_std_17)
