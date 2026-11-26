find_package(Qt6 REQUIRED COMPONENTS Core)

# zlib / sqlite3 由顶层 Dependencies 解析（ps_zlib / ps_sqlite3）。
# 这里只做兜底：单独把 tests/ 当独立工程用时退回系统库。
if(TARGET ps_zlib)
    set(PS_TEST_ZLIB ps_zlib)
else()
    find_package(ZLIB REQUIRED)
    set(PS_TEST_ZLIB ZLIB::ZLIB)
endif()
if(TARGET ps_sqlite3)
    set(PS_TEST_SQLITE ps_sqlite3)
else()
    find_package(SQLite3 REQUIRED)
    set(PS_TEST_SQLITE SQLite::SQLite3)
endif()

# === ps_dataset 独立链接验收（B2）===
# 故意只链 ps_dataset + Qt6::Core，不链 ps_context / host / module / extension。
# 任何 Dataset 对 core 业务类型的反向依赖都会在此链接失败。
add_executable(ps_test_dataset_standalone test_dataset_standalone.cpp)
target_link_libraries(ps_test_dataset_standalone PRIVATE ps_dataset Qt6::Core)
set_target_properties(ps_test_dataset_standalone PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_dataset_standalone COMMAND ps_test_dataset_standalone)

# === PersistentStateHub 验收（M4-R2）：往返 / 分段 / 登记序 / bestEffort ===
add_executable(ps_test_persistent_state test_persistent_state.cpp)
target_link_libraries(ps_test_persistent_state PRIVATE ps_context Qt6::Core)
set_target_properties(ps_test_persistent_state PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_persistent_state COMMAND ps_test_persistent_state)

# === DataTableModel 验收（2.2.2）：懒加载分页 + 百万行 <100ms ===
# 只链 ps_data_tablemodel + ps_dataset + Qt Core；模型碰 Widgets 即编译失败。
add_executable(ps_test_data_table test_data_table.cpp)
target_link_libraries(ps_test_data_table PRIVATE ps_data_tablemodel ps_dataset Qt6::Core)
set_target_properties(ps_test_data_table PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_data_table COMMAND ps_test_data_table)

# === ps_command 验收（IKXGYH 基线 13）：when 白名单语法 / 编译缓存 / 三层作用域 / 键位冲突 ===
add_executable(ps_test_command test_command.cpp)
target_link_libraries(ps_test_command PRIVATE ps_command)
set_target_properties(ps_test_command PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_command COMMAND ps_test_command)

# === ps_registry 独立链接验收（B3）===
# 故意只链 ps_registry + Qt6::Core，不链 ps_context / host / module。
# 任何注册表核对 core 业务类型/QWidget/Rust 的反向依赖都会在此编译或链接失败。
add_executable(ps_test_registry_standalone test_registry_standalone.cpp)
target_link_libraries(ps_test_registry_standalone PRIVATE ps_registry Qt6::Core)
set_target_properties(ps_test_registry_standalone PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_registry_standalone COMMAND ps_test_registry_standalone)

# A8b-M1 贡献解析：只链 ps_registry + Qt6::Core，纯解析无副作用。
add_executable(ps_test_contribution_parser test_contribution_parser.cpp)
target_link_libraries(ps_test_contribution_parser PRIVATE ps_registry Qt6::Core)
set_target_properties(ps_test_contribution_parser PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_contribution_parser COMMAND ps_test_contribution_parser)

# A8b-M1 扩展注册表的 contributes 属性面：mount/unmount/owner 索引/整批撤回，只链 ps_registry。
add_executable(ps_test_extension_contributions test_extension_contributions.cpp)
target_link_libraries(ps_test_extension_contributions PRIVATE ps_registry Qt6::Core)
set_target_properties(ps_test_extension_contributions PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_extension_contributions COMMAND ps_test_extension_contributions)

# IConfigurationService：分层合并 / reset 单键 / unmount 残留诊断。
# 链 ps_assembly（adapter 头文件内联）+ Qt6::Core；SettingsStore 内存态，不碰 GUI。
add_executable(ps_test_configuration_service test_configuration_service.cpp)
target_link_libraries(ps_test_configuration_service PRIVATE ps_assembly Qt6::Core)
set_target_properties(ps_test_configuration_service PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_configuration_service COMMAND ps_test_configuration_service)

# A8b-M1 扩展扫描：扫目录读 manifest.json，只登记 contributes，不链 GUI/toolbox。
add_executable(ps_test_extension_scanner test_extension_scanner.cpp)
target_link_libraries(ps_test_extension_scanner PRIVATE ps_registry Qt6::Core)
set_target_properties(ps_test_extension_scanner PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_extension_scanner COMMAND ps_test_extension_scanner)

# A8b-M2 视图注册表：视图声明 + registerViewProvider + unmount 整批撤，只链 ps_registry。
add_executable(ps_test_view_registry test_view_registry.cpp)
target_link_libraries(ps_test_view_registry PRIVATE ps_registry Qt6::Core)
set_target_properties(ps_test_view_registry PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_view_registry COMMAND ps_test_view_registry)

# A8b-M2 契约面：ISubscriptions/SubscriptionBag（补建 M1 e0 欠账接口）。
add_executable(ps_test_subscriptions test_subscriptions.cpp)
target_link_libraries(ps_test_subscriptions PRIVATE ps_workbench Qt6::Core)
set_target_properties(ps_test_subscriptions PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_subscriptions COMMAND ps_test_subscriptions)

# 验收 4：面板引用三源启动期校验（包 PanelSpec / Activity panelTypes+centralPanel /
# 产品 defaultPanels）；负向用例故意声明不存在的面板类型，断言校验器真的报错。
add_executable(ps_test_panel_ref test_panel_ref.cpp)
target_link_libraries(ps_test_panel_ref PRIVATE ps_registry Qt6::Core)
set_target_properties(ps_test_panel_ref PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_panel_ref COMMAND ps_test_panel_ref)

# 意图契约独立验收：只链 ps_intent + foundation + Qt Core，不链 ps_context。
add_executable(ps_test_intent_standalone test_intent_standalone.cpp)
target_link_libraries(ps_test_intent_standalone PRIVATE ps_intent ps_foundation Qt6::Core)
set_target_properties(ps_test_intent_standalone PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_intent_standalone COMMAND ps_test_intent_standalone)

# schema 契约独立验收：只链 ps_uischema + foundation + Qt Core（headless，不拉 Widgets）。
add_executable(ps_test_uischema_standalone test_uischema_standalone.cpp)
target_link_libraries(ps_test_uischema_standalone PRIVATE ps_uischema ps_foundation Qt6::Core)
set_target_properties(ps_test_uischema_standalone PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_uischema_standalone COMMAND ps_test_uischema_standalone)

# TransformParam -> ParamSpec 适配链路验收（headless，链 core 但不拉 GUI）。
add_executable(ps_test_transform_adapter test_transform_adapter.cpp)
target_link_libraries(ps_test_transform_adapter PRIVATE ps_context ps_uischema Qt6::Core)
set_target_properties(ps_test_transform_adapter PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_transform_adapter COMMAND ps_test_transform_adapter)

# B8-a：Lua 脚本库 describe() 加载器验收（headless，链 script 模块 + core + foundation）。
add_executable(ps_test_lua_script_library test_lua_script_library.cpp)
target_link_libraries(ps_test_lua_script_library PRIVATE
    ps_module_script ps_context ps_uischema ps_foundation Qt6::Core)
set_target_properties(ps_test_lua_script_library PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_lua_script_library COMMAND ps_test_lua_script_library)

# M2.1：LuaEngine REPL 求值 + 输出 sink 验收（headless）。
add_executable(ps_test_lua_engine test_lua_engine.cpp)
target_link_libraries(ps_test_lua_engine PRIVATE
    ps_module_script ps_context ps_foundation Qt6::Core)
set_target_properties(ps_test_lua_engine PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_lua_engine COMMAND ps_test_lua_engine)

# 有状态 Lua 过滤器引擎验收（headless，对齐 PJ4 FilterClass 契约）。
add_executable(ps_test_filter_engine test_filter_engine.cpp)
target_link_libraries(ps_test_filter_engine PRIVATE
    ps_module_script ps_context ps_foundation Qt6::Core)
set_target_properties(ps_test_filter_engine PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_filter_engine COMMAND ps_test_filter_engine)

# 内置过滤器库 golden 验收（12 个 bundled Luau 过滤器，对齐 PJ4 builtin_golden_test）。
add_executable(ps_test_filter_catalogue test_filter_catalogue.cpp)
target_link_libraries(ps_test_filter_catalogue PRIVATE
    ps_module_script ps_context ps_foundation Qt6::Core)
set_target_properties(ps_test_filter_catalogue PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_filter_catalogue COMMAND ps_test_filter_catalogue)

# Marker 规则引擎 golden 验收（对齐 PJ4 marker_engine_test.cpp 的 DSL 语义）。
add_executable(ps_test_marker_engine test_marker_engine.cpp)
target_link_libraries(ps_test_marker_engine PRIVATE
    ps_module_script ps_context ps_foundation Qt6::Core)
set_target_properties(ps_test_marker_engine PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_marker_engine COMMAND ps_test_marker_engine)

# M2.6 Python 子进程桥（headless）：eval/崩溃隔离/超时强杀/shm 基准/写回 v2。
# 依赖宿主 python3 + numpy（找不到解释器时 status 明确报错，属环境问题非代码缺陷）。
add_executable(ps_test_python test_python.cpp)
target_include_directories(ps_test_python PRIVATE ${CMAKE_SOURCE_DIR}/src/application/app/core)
target_link_libraries(ps_test_python PRIVATE
    ps_module_data ps_module_plot ps_module_script ps_module_protocol ps_module_analysis ps_module_settings ps_module_report ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    # ps_app 已包含 App.cpp 且只编译一次；这里只链接，不再重复吃源码
    ps_app ps_host ps_context ps_recording Qt6::Core)
set_target_properties(ps_test_python PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_python COMMAND ps_test_python)

# M2.4 批量处理验收（headless，走真实执行路径）：
# data.batch.run 失败明细 + 进度广播 + 分片边界取消 + keep 保留。
add_executable(ps_test_batch test_batch.cpp)
target_include_directories(ps_test_batch PRIVATE ${CMAKE_SOURCE_DIR}/src/application/app/core)
target_link_libraries(ps_test_batch PRIVATE
    ps_module_data ps_module_plot ps_module_script ps_module_protocol ps_module_analysis ps_module_settings ps_module_report ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_app ps_host ps_context ps_recording Qt6::Core)
set_target_properties(ps_test_batch PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_batch COMMAND ps_test_batch)

# M2.8：RegistryNode/RegistryTree 机制验收（上树/摘树/kind 查找/信号/树隔离）
# + diag.registries 意图验收（真实执行路径：App 装配 + dispatchViewAction）。
add_executable(ps_test_registry_node test_registry_node.cpp)
target_include_directories(ps_test_registry_node PRIVATE ${CMAKE_SOURCE_DIR}/src/application/app/core)
target_link_libraries(ps_test_registry_node PRIVATE
    ps_module_data ps_module_plot ps_module_script ps_module_protocol ps_module_analysis ps_module_settings ps_module_report ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_registry ps_app ps_host ps_context ps_recording Qt6::Core)
set_target_properties(ps_test_registry_node PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_registry_node COMMAND ps_test_registry_node)

# 状态视图 golden 验收（对齐 PJ4 StateColors/StateSeriesAdapter/StateTransitionsView）。
add_executable(ps_test_state_view test_state_view.cpp)
target_link_libraries(ps_test_state_view PRIVATE
    fastplot Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_test_state_view PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_state_view COMMAND ps_test_state_view)
set_tests_properties(ps_state_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# B8-b：TransformField 输入控件输出契约验收（offscreen GUI，链 gui_transform + core）。
add_executable(ps_test_transform_field test_transform_field.cpp)
target_link_libraries(ps_test_transform_field PRIVATE
    ps_widgets ps_context ps_uischema_widgets Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_test_transform_field PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_transform_field COMMAND ps_test_transform_field)
set_tests_properties(ps_transform_field PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# === 协议逻辑资产（顺序段协议解码内核平移）===
# 纯逻辑测试：只依赖 ps_protocol + Qt Core，headless。
add_executable(ps_test_protocol test_protocol.cpp)
target_link_libraries(ps_test_protocol PRIVATE
    ps_protocol ps_toolbox_common ps_host ps_context ps_recording Qt6::Core)
target_compile_definitions(ps_test_protocol PRIVATE
    PS_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixture")
set_target_properties(ps_test_protocol PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_protocol COMMAND ps_test_protocol)
# 含性能断言（设计 §9.7 第1条：单轮解析 ≤ 5ms）。该断言度量的是单轮最坏耗时，
# 与并行调度强相关：-j4 下其他测试抢 CPU 时最坏轮次会被拉长到 5ms 以上而误红
# （CI 首次门禁即此现象）。用 RUN_SERIAL 让它在独占 CPU 下跑，保住断言不放宽。
set_tests_properties(ps_protocol PROPERTIES RUN_SERIAL TRUE)

# === 协议模块意图面（ng/modules/protocol 建立在 ps_protocol 之上）===
# headless：只链模块 + 库 + foundation，不拉 Widgets。
# 覆盖 4 个 op 的正常/拒绝双路：op 认领、validate 分阶段、preview 双入口、模板库往返。
add_executable(ps_test_protocol_module test_protocol_module.cpp)
target_link_libraries(ps_test_protocol_module PRIVATE
    ps_module_protocol ps_protocol ps_host ps_context ps_foundation Qt6::Core)
set_target_properties(ps_test_protocol_module PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_protocol_module COMMAND ps_test_protocol_module)
add_executable(ps_test_architecture test_architecture.cpp)
target_include_directories(ps_test_architecture PRIVATE ${CMAKE_SOURCE_DIR}/src/application/app/core)
# OBJECT 库必须显式链接：模块/知识包靠 TU 静态对象自注册。
target_link_libraries(ps_test_architecture PRIVATE
    ps_module_data ps_module_plot ps_module_script ps_module_protocol ps_module_analysis ps_module_settings ps_module_report ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_app ps_host ps_context ps_recording Qt6::Core)
set_target_properties(ps_test_architecture PROPERTIES AUTOMOC ON CXX_STANDARD 17)
# D18a 归属断言需要源码根（ctest 的 cwd 是 build 目录，__FILE__ 不可靠）。
target_compile_definitions(ps_test_architecture PRIVATE PS_TEST_SRC_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME ps_architecture COMMAND ps_test_architecture)

# === 统一变换 / Selection / 能力防错契约（headless）===
# 链 ps_module_script 触发 TransformEvaluator 注入；表达式走 Rust，Lua 走 C++。
add_executable(ps_test_transform test_transform.cpp)
target_include_directories(ps_test_transform PRIVATE ${CMAKE_SOURCE_DIR}/src/application/app/core)
target_link_libraries(ps_test_transform PRIVATE
    ps_module_data ps_module_plot ps_module_script ps_module_protocol ps_module_analysis ps_module_settings ps_module_report ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_app ps_host ps_context ps_recording Qt6::Core)
set_target_properties(ps_test_transform PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_transform COMMAND ps_test_transform)

# === GUI 契约测试 ===
# 独立可执行文件：GUI 依赖 Qt Widgets，与 headless 测试分开，
# 保证 headless（CLI）构建不被 Widgets 污染。
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

# UI 自动化驱动工具（offscreen 截图 + Qt 事件注入）。不是 ctest 用例：
# 由人工/脚本带 JSON 步骤文件运行，用法见 docs/develop/testing.md。
add_executable(ps_ui_driver ui_driver.cpp)
target_include_directories(ps_ui_driver PRIVATE ${CMAKE_SOURCE_DIR}/src/application/app/core)
target_link_libraries(ps_ui_driver PRIVATE
    ps_gui ps_gui_theme ps_widgets
    ps_module_data ps_module_data_gui ps_module_plot ps_module_plot_gui ps_module_script ps_module_script_gui ps_module_protocol ps_module_protocol_gui ps_module_analysis ps_module_settings ps_module_report ps_module_report_gui ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_toolbox_automotive_gui
    ps_app ps_host ps_context ps_recording Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Test)
set_target_properties(ps_ui_driver PROPERTIES AUTOMOC ON CXX_STANDARD 17)

add_executable(ps_test_gui test_gui.cpp)
target_include_directories(ps_test_gui PRIVATE ${CMAKE_SOURCE_DIR}/src/application/app/core)
# 领域面板的呈现层：领域面板的 QWidget 实现按设计只在 GUI 侧链接。
# 测试要验证"描述 + 实现共用一张工厂表"，就必须像真正的 GUI 一样链接它；
# headless 测试（ps_test_architecture）故意不链，借此守住 CLI 的 headless 纯净。
target_link_libraries(ps_test_gui PRIVATE
    ps_gui
    # 主题库：OBJECT 库之间不传播目标文件，必须显式链进可执行文件。
    ps_gui_theme
    ps_widgets
    ps_module_data ps_module_data_gui ps_module_plot ps_module_plot_gui ps_module_script ps_module_script_gui ps_module_protocol ps_module_protocol_gui ps_module_analysis ps_module_settings ps_module_report ps_module_report_gui ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    # 领域面板呈现层（OBJECT 库）：自注册 TU 必须显式链接进来。
    ps_toolbox_automotive_gui
    ps_app ps_host ps_context ps_recording Qt6::Widgets Qt6::Gui Qt6::Core)
# 测试数据目录：仓库根 tests/fixture（ng 自带 fixture 会重复同一批样本）。
# 数据源面板的契约测试必须走**真实加载路径**（data.load 意图），
# 因为"数据集归属哪个源"只有走摄取管道才会产生——直接往仓库塞数据集测不到。
target_compile_definitions(ps_test_gui PRIVATE
    PS_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixture")
set_target_properties(ps_test_gui PROPERTIES AUTOMOC ON CXX_STANDARD 17)
# offscreen 平台插件：无显示环境（CI）也能跑。
add_test(NAME ps_gui COMMAND ps_test_gui)
set_tests_properties(ps_gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# A8b-M1 e2e cmd-ext：内置扩展一条命令 → 命令面板接线 → 执行成功 → 卸载撤回。
# 不构造完整 MainWindow，走 gui_main 同款 binder 装配；不链业务模块（空壳底座即可跑）。
add_executable(ps_test_cmd_ext e2e/cmd-ext/test_cmd_ext.cpp)
target_include_directories(ps_test_cmd_ext PRIVATE
    ${CMAKE_SOURCE_DIR}/src/application/app/core
    ${CMAKE_SOURCE_DIR}/src/application/assembly/include)
target_link_libraries(ps_test_cmd_ext PRIVATE
    ps_gui ps_gui_theme ps_widgets
    ps_app ps_assembly ps_host ps_context ps_command ps_registry Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_test_cmd_ext PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_cmd_ext COMMAND ps_test_cmd_ext)
set_tests_properties(ps_cmd_ext PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# === 新架构契约测试（Source 多数据源 + Parser SPI）===
# 独立可执行文件：只依赖 core + 扩展里的解析器，不需要 GUI。
add_executable(ps_test_pipeline test_pipeline.cpp)
target_include_directories(ps_test_pipeline PRIVATE
    ${CMAKE_SOURCE_DIR}/src/toolbox/common)
target_compile_definitions(ps_test_pipeline PRIVATE
    PS_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixture")
target_link_libraries(ps_test_pipeline PRIVATE
    ps_recording
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_host ps_context ps_recording Qt6::Core)
set_target_properties(ps_test_pipeline PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_pipeline COMMAND ps_test_pipeline)

# === fastplot 绘图库契约测试 ===
# 与 ps_test_gui 同理：Widgets 侧测试单独成可执行文件，offscreen 下跑。
add_executable(fastplot_test test_fastplot.cpp)
target_link_libraries(fastplot_test PRIVATE fastplot Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(fastplot_test PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME fastplot COMMAND fastplot_test)
set_tests_properties(fastplot PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# === fastplot 术语对齐（Figure/Axes/Line/Bar + artist 模型）===
add_executable(fastplot_api_test test_fastplot_api.cpp)
target_link_libraries(fastplot_api_test PRIVATE fastplot Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(fastplot_api_test PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME fastplot_api COMMAND fastplot_api_test)
set_tests_properties(fastplot_api PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# === fastplot × ng 集成测试（真实 ULog → 内核 LOD → 绘制）===
# 这是"库能被 ng 直接调用"的可执行证明：链接 core + 扩展解析器，不碰老 src。
add_executable(fastplot_dataset_test test_fastplot_dataset.cpp)
# fixture 路径由 CMake 注入（随仓库走，不写死机器路径）
target_compile_definitions(fastplot_dataset_test PRIVATE
    PS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixture")
target_link_libraries(fastplot_dataset_test PRIVATE
    fastplot
    ps_recording
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_host ps_context ps_recording Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(fastplot_dataset_test PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME fastplot_dataset COMMAND fastplot_dataset_test)
set_tests_properties(fastplot_dataset PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# === fastplot 仪表族（P5）：六个部件各画各的，互不认识 ===
add_executable(fastplot_instruments_test test_fastplot_instruments.cpp)
target_link_libraries(fastplot_instruments_test PRIVATE fastplot Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(fastplot_instruments_test PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME fastplot_instruments COMMAND fastplot_instruments_test)
set_tests_properties(fastplot_instruments PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# === plot.instrument 意图 + 仪表视图（P5 接进 ng）===
add_executable(ps_test_instrument test_instrument.cpp)
target_include_directories(ps_test_instrument PRIVATE ${CMAKE_SOURCE_DIR}/src/application/app/core)
# fixture 路径由 CMake 注入（随仓库走，不写死机器路径）
target_compile_definitions(ps_test_instrument PRIVATE
    PS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixture")
target_link_libraries(ps_test_instrument PRIVATE
    ps_gui
    # 主题库：OBJECT 库之间不传播目标文件，必须显式链进可执行文件。
    ps_gui_theme
    ps_widgets
    ps_module_data ps_module_data_gui ps_module_plot ps_module_plot_gui ps_module_script ps_module_script_gui ps_module_protocol ps_module_protocol_gui ps_module_analysis ps_module_settings ps_module_report ps_module_report_gui ps_module_workspace
    ps_recording
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_app ps_host ps_context ps_recording Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_test_instrument PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_instrument COMMAND ps_test_instrument)
set_tests_properties(ps_instrument PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# === 画布导出 / 视图状态 / 图例（P6：库能力接进 ng 视图层）===
add_executable(ps_test_plot_io test_plot_io.cpp)
target_include_directories(ps_test_plot_io PRIVATE ${CMAKE_SOURCE_DIR}/src/application/app/core)
target_link_libraries(ps_test_plot_io PRIVATE
    ps_gui
    # 主题库：OBJECT 库之间不传播目标文件，必须显式链进可执行文件。
    ps_gui_theme
    ps_widgets
    ps_module_data ps_module_data_gui ps_module_plot ps_module_plot_gui ps_module_script ps_module_script_gui ps_module_protocol ps_module_protocol_gui ps_module_analysis ps_module_settings ps_module_report ps_module_report_gui ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_recording
    ps_app ps_host ps_context ps_recording Qt6::Widgets Qt6::Gui Qt6::Core)
target_compile_definitions(ps_test_plot_io PRIVATE PS_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixture")
set_target_properties(ps_test_plot_io PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_plot_io COMMAND ps_test_plot_io)
set_tests_properties(ps_plot_io PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Windows：把 ads.dll 拷到本目录各可执行目标旁边，构建完即可直接运行/跑测试。
ps_deploy_windows_runtime_all()


# === MCP stdio 桥契约测试 ===
# 直接调 mcp_bridge.cpp 的纯函数 handleRequest（不经 stdin/stdout）。
# 覆盖 initialize/ping/tools/list schema 转换/tools/call 正常与 isError/未知 method/
# notification 静默/错误响应构造；同时验证 #215 的 read_series 工具在目录里。
add_executable(ps_test_mcp_bridge test_mcp_bridge.cpp)
target_include_directories(ps_test_mcp_bridge PRIVATE ${CMAKE_SOURCE_DIR}/src/application/app/core ${CMAKE_SOURCE_DIR}/src/application/app/mcp)
# App.cpp 与 mcp_bridge.cpp 都已在 ps_app 里（各自只编一次），链接即可
target_link_libraries(ps_test_mcp_bridge PRIVATE
    ps_module_data ps_module_plot ps_module_script ps_module_protocol ps_module_analysis ps_module_settings ps_module_report ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_app ps_host ps_context ps_recording Qt6::Core)
target_compile_definitions(ps_test_mcp_bridge PRIVATE
    PS_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixture")
set_target_properties(ps_test_mcp_bridge PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_mcp_bridge COMMAND ps_test_mcp_bridge)
add_executable(ps_test_live_udp test_live_udp.cpp)
target_include_directories(ps_test_live_udp PRIVATE ${CMAKE_SOURCE_DIR}/src/application/app/core)
target_link_libraries(ps_test_live_udp PRIVATE
    ps_gui ps_gui_theme ps_widgets
    ps_module_data ps_module_data_gui ps_module_plot ps_module_plot_gui ps_module_script ps_module_script_gui ps_module_protocol ps_module_protocol_gui ps_module_analysis ps_module_settings ps_module_report ps_module_report_gui ps_module_workspace
    ps_toolbox_common ps_toolbox_autopilot ps_toolbox_automotive ps_parser_mdf ps_parser_rosbag2 ps_parser_blf ps_parser_apbin ps_toolbox_robotics ps_toolbox_gnss
    ps_toolbox_automotive_gui
    ps_app ps_host ps_context ps_recording Qt6::Widgets Qt6::Gui Qt6::Core)
# libutil 提供 pty.h 的 openpty，只有 Linux 有；Windows 侧该用例的 pty 分支已被 #ifdef 排除。
if(UNIX)
    target_link_libraries(ps_test_live_udp PRIVATE util)
endif()
set_target_properties(ps_test_live_udp PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_test_live_udp COMMAND ps_test_live_udp)

# 报告模板贡献独立验收（M2.3 2.3.1）：只链 ps_registry + Qt Core，headless。
add_executable(ps_test_report_template test_report_template.cpp)
target_link_libraries(ps_test_report_template PRIVATE ps_registry Qt6::Core)
set_target_properties(ps_test_report_template PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_report_template COMMAND ps_test_report_template)

# MDF 4.x 解析器（T6.1）：SPI 直连验收，headless；fixture 用 ##DZ 压缩文件。
add_executable(ps_test_mdf_parser test_mdf_parser.cpp)
target_link_libraries(ps_test_mdf_parser PRIVATE ps_parser_mdf ps_parser_rosbag2 Qt6::Core ${PS_TEST_ZLIB})
target_compile_definitions(ps_test_mdf_parser PRIVATE
    PS_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixture")
set_target_properties(ps_test_mdf_parser PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_mdf_parser COMMAND ps_test_mdf_parser)

# ROS2 bag 解析器（T6.2）：SPI 直连验收，headless；fixture 由 make_rosbag2_demo.py 生成。
add_executable(ps_test_rosbag2_parser test_rosbag2_parser.cpp)
target_link_libraries(ps_test_rosbag2_parser PRIVATE ps_parser_rosbag2 Qt6::Core ${PS_TEST_SQLITE})
target_compile_definitions(ps_test_rosbag2_parser PRIVATE
    PS_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixture")
set_target_properties(ps_test_rosbag2_parser PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_rosbag2_parser COMMAND ps_test_rosbag2_parser)

# Vector BLF 解析器（T6.3）：SPI 直连验收，headless；fixture 为仓库内 demo_can.blf。
add_executable(ps_test_blf_parser test_blf_parser.cpp)
target_link_libraries(ps_test_blf_parser PRIVATE ps_parser_blf Qt6::Core ${PS_TEST_ZLIB})
target_compile_definitions(ps_test_blf_parser PRIVATE
    PS_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixture")
set_target_properties(ps_test_blf_parser PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_blf_parser COMMAND ps_test_blf_parser)

# ArduPilot DataFlash（apbin）解析器（T6.3）：SPI 直连验收，headless；fixture 为 demo_apbin.bin。
add_executable(ps_test_apbin_parser test_apbin_parser.cpp)
target_link_libraries(ps_test_apbin_parser PRIVATE ps_parser_apbin Qt6::Core)
target_compile_definitions(ps_test_apbin_parser PRIVATE
    PS_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixture")
set_target_properties(ps_test_apbin_parser PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_apbin_parser COMMAND ps_test_apbin_parser)

# Arrow 导入导出通道（S6）：标准 nanoarrow IPC reader 消费 + 时间归一 + null 往返，headless。
add_executable(ps_test_arrow test_arrow.cpp)
target_link_libraries(ps_test_arrow PRIVATE ps_arrow ps_dataset ps_nanoarrow Qt6::Core)
set_target_properties(ps_test_arrow PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
add_test(NAME ps_arrow COMMAND ps_test_arrow)

# 裸壳冒烟（验收 6/18/19/20）：ps_shell_bare --smoke 在 offscreen 下建窗、
# 跑一轮事件循环后退出；壳内零模块装配，warning/critical/fatal 任一即非零退出。
# PS_BUILD_BARE=OFF 时该用例一并跳过（只编完整产品）。
if(PS_BUILD_BARE AND TARGET ps_shell_bare)
    add_test(NAME ps_shell_bare_smoke COMMAND ps_shell_bare --smoke)
    set_tests_properties(ps_shell_bare_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# 状态栏登记处（验收 7）：headless，纯数据，不链 Widgets。
add_executable(ps_test_status_hub test_status_hub.cpp)
target_link_libraries(ps_test_status_hub PRIVATE ps_context Qt6::Core Qt6::Test)
set_target_properties(ps_test_status_hub PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ps_status_hub COMMAND ps_test_status_hub)

# ps_instantiation 独立验收（IKXUTD A1b §1.1）：注册/类型安全取用/同 id 拒绝/倒序撤。
add_executable(ps_test_instantiation test_instantiation.cpp)
target_link_libraries(ps_test_instantiation PRIVATE ps_instantiation Qt6::Core)
set_target_properties(ps_test_instantiation PROPERTIES AUTOMOC OFF CXX_STANDARD 17)
add_test(NAME ps_instantiation COMMAND ps_test_instantiation)

# ps_workbench 契约验收（IKXUTD A1b §1.3）：接口可编译 + NullWindow 全 no-op + Subscription。
add_executable(ps_test_workbench_contracts test_workbench_contracts.cpp)
target_link_libraries(ps_test_workbench_contracts PRIVATE ps_workbench Qt6::Core)
set_target_properties(ps_test_workbench_contracts PROPERTIES AUTOMOC OFF CXX_STANDARD 17)
add_test(NAME ps_workbench_contracts COMMAND ps_test_workbench_contracts)

# —— GUI 类测试串行化（抖动治理）——
# 这批用例各自要起完整 QApplication + MainWindow + dock 布局，在 offscreen 平台上还会
# 共用平台插件与字体缓存的全局状态。CI 用 ctest -j$(nproc)，4 核上十几个这类进程一起
# 起来会争 CPU/内存与这些共享资源：ps_gui 出现过「同一提交一次 SEGFAULT、重跑即过」的
# 抖动。RESOURCE_LOCK 只让持锁者之间互斥，纯逻辑测试照常并行，总时长几乎不变。
set(_ps_gui_tests
    ps_state_view
    ps_transform_field
    ps_gui
    ps_cmd_ext
    fastplot
    fastplot_api
    fastplot_dataset
    fastplot_instruments
    ps_instrument
    ps_plot_io)
set_tests_properties(${_ps_gui_tests} PROPERTIES RESOURCE_LOCK ps_gui)

# 裸壳冒烟同样要建窗，但它只在 PS_BUILD_BARE 下定义，锁单独挂。
if(PS_BUILD_BARE AND TARGET ps_shell_bare)
    set_tests_properties(ps_shell_bare_smoke PROPERTIES RESOURCE_LOCK ps_gui)
endif()

# Windows：测试目录也要部署 ads.dll。application 的三个目录各自调用过
# ps_deploy_windows_runtime_all()，但 tests 目录此前没有——CI 在 Windows 上跑 ctest 时
# 可执行找不到 ads.dll 会直接 0xc0000135（STATUS_DLL_NOT_FOUND），只能手工往 PATH 塞目录。
# 现在 Windows 腿要真跑测试，这条必须补上。非 Windows 平台函数直接空转。
ps_deploy_windows_runtime_all()
