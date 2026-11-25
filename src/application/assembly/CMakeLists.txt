# 组合根（装配层）：完整产品的可执行在这里装配。
# 壳（ps_gui）只链 libs 层；模块/工具箱/解析器的链接清单属于**装配**，不属于壳，
# 因此本文件是仓库里唯一允许出现 ps_module_* / ps_toolbox_* / ps_parser_* 的 gui 侧文件。
# （壳目录 src/application/gui 的 grep 必须为 0，见契约 §11 链接关 / 验收 6。）

# ps_assembly（A1b IKXUTD §1.4）：workbench 服务装配 glue。
# 持有 InstantiationService、把具体 Hub 包成 workbench 接口、注册 3 条 VSCode 服务。
# 只被组合根（ps_app）链接；模块只见 workbench 契约，不见具体 Hub。
add_library(ps_assembly STATIC
    include/WorkbenchAdapters.h
    include/WorkbenchServices.h
    include/ExtensionActivationService.h
    src/WorkbenchAdapters.cpp
    src/WorkbenchServices.cpp
    src/ExtensionActivationService.cpp
)
target_include_directories(ps_assembly PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ps_assembly PUBLIC
    ps_workbench ps_instantiation ps_host ps_context ps_intent ps_command ps_dataset ps_recording
    ps_registry ps_foundation Qt6::Core)
set_target_properties(ps_assembly PROPERTIES AUTOMOC ON CXX_STANDARD 17)

# 反证开关（A4a）：PS_DISABLE_MODULE 接受分号分隔的 target 名列表，装配时剔除。
# 用途：CI 逐个剔除任一 ps_module_* 后完整产品仍能 build/run——证明模块间无横向依赖、
# 壳不依赖模块。日常构建不设此变量，全模块装配。
set(PS_ASSEMBLY_MODULES
    ps_module_data ps_module_data_gui ps_module_plot ps_module_plot_gui
    ps_module_script ps_module_script_gui ps_module_protocol ps_module_protocol_gui
    ps_module_analysis ps_module_settings ps_module_report ps_module_report_gui
    ps_module_workspace)
set(PS_PRODUCT_MODULES ${PS_ASSEMBLY_MODULES})
if(PS_DISABLE_MODULE)
    foreach(_disabled IN LISTS PS_DISABLE_MODULE)
        list(REMOVE_ITEM PS_PRODUCT_MODULES ${_disabled})
    endforeach()
    message(STATUS "[装配] 反证模式：已剔除模块 [${PS_DISABLE_MODULE}]，剩余 ${CMAKE_CURRENT_LIST_DIR}")
endif()

add_executable(plotscope-gui "${CMAKE_SOURCE_DIR}/src/application/gui/gui_main.cpp")
# 装配层对模块的弱依赖：gui_main 里少数"模块呈现层装配钩子"用宏守护，
# 反证剔除该模块时对应 include + 调用被编译掉，完整产品仍可 build（A4a）。
if("ps_module_report_gui" IN_LIST PS_PRODUCT_MODULES)
    target_compile_definitions(plotscope-gui PRIVATE PS_HAS_REPORT_GUI=1)
endif()
target_link_libraries(plotscope-gui PRIVATE
    ps_app
    ps_gui
    # 主题库必须**显式链到可执行文件**：OBJECT 库链 OBJECT 库只传播 usage
    # requirements，不传播目标文件——不显式链则 ThemeManager 的实现不会进二进制。
    ps_gui_theme
    ps_widgets
    # 装配清单：模块 + 工具箱 + 解析器（产品的"业务"全部来自这里）。
    ${PS_PRODUCT_MODULES}
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    # 领域面板的呈现层：只 GUI 链接，CLI 不含（保持 headless 纯净）。
    ps_toolbox_automotive_gui
    ps_host ps_context ps_recording Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(plotscope-gui PROPERTIES AUTOMOC ON CXX_STANDARD 17)

ps_deploy_windows_runtime_all()
