# ps_workbench：工作台契约库（VSCode workbench 对位）。
# 放 access 接口、vscode.* 服务接口、IWorkbenchContribution、Event 支撑类型。
# 纯契约：只依赖 libs 地基（dataset/intent/recording/instantiation）+ Qt Core，
# 不含任何具体 Hub 类型、不依赖 context/host/gui，headless 可用。
add_library(ps_workbench STATIC
    include/Subscription.h
    include/IDatasetAccess.h
    include/ISourceAccess.h
    include/IIngest.h
    include/IRecorder.h
    include/IPlayback.h
    include/VsCodeTypes.h
    include/IWorkspace.h
    include/ICommands.h
    include/IWindow.h
    include/NullWindow.h
    include/CommandAdapter.h
    include/IWorkbenchContribution.h
)

target_include_directories(ps_workbench PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# ps_context：IWorkspace::derived/configuration 与 IWindow::state 返回的 libs 具体类型，
# NullWindow 持有 PersistentStateHub；context 不反向依赖 workbench，无环。
target_link_libraries(ps_workbench PUBLIC
    ps_instantiation ps_dataset ps_intent ps_recording ps_context ps_module ps_foundation Qt6::Core)
set_target_properties(ps_workbench PROPERTIES CXX_STANDARD 17 AUTOMOC OFF)
