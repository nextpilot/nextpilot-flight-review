find_package(Qt6 REQUIRED COMPONENTS Core)

# === ng_dataset 独立链接验收（B2）===
# 故意只链 ng_dataset + Qt6::Core，不链 ng_core / host / module / extension。
# 任何 Dataset 对 core 业务类型的反向依赖都会在此链接失败。
add_executable(ng_test_dataset_standalone test_dataset_standalone.cpp)
target_link_libraries(ng_test_dataset_standalone PRIVATE ng_dataset Qt6::Core)
set_target_properties(ng_test_dataset_standalone PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_dataset_standalone COMMAND ng_test_dataset_standalone)

# === ng_registry 独立链接验收（B3）===
# 故意只链 ng_registry + Qt6::Core，不链 ng_core / host / module。
# 任何注册表核对 core 业务类型/QWidget/Rust 的反向依赖都会在此编译或链接失败。
add_executable(ng_test_registry_standalone test_registry_standalone.cpp)
target_link_libraries(ng_test_registry_standalone PRIVATE ng_registry Qt6::Core)
set_target_properties(ng_test_registry_standalone PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_registry_standalone COMMAND ng_test_registry_standalone)

# 意图契约独立验收：只链 ng_intent + foundation + Qt Core，不链 ng_core。
add_executable(ng_test_intent_standalone test_intent_standalone.cpp)
target_link_libraries(ng_test_intent_standalone PRIVATE ng_intent ng_foundation Qt6::Core)
set_target_properties(ng_test_intent_standalone PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_intent_standalone COMMAND ng_test_intent_standalone)

# schema 契约独立验收：只链 ng_uischema + foundation + Qt Core（headless，不拉 Widgets）。
add_executable(ng_test_uischema_standalone test_uischema_standalone.cpp)
target_link_libraries(ng_test_uischema_standalone PRIVATE ng_uischema ng_foundation Qt6::Core)
set_target_properties(ng_test_uischema_standalone PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_uischema_standalone COMMAND ng_test_uischema_standalone)

# TransformParam -> ParamSpec 适配链路验收（headless，链 core 但不拉 GUI）。
add_executable(ng_test_transform_adapter test_transform_adapter.cpp)
target_link_libraries(ng_test_transform_adapter PRIVATE ng_core ng_uischema Qt6::Core)
set_target_properties(ng_test_transform_adapter PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_transform_adapter COMMAND ng_test_transform_adapter)

# B8-a：Lua 脚本库 describe() 加载器验收（headless，链 script 模块 + core + foundation）。
add_executable(ng_test_lua_script_library test_lua_script_library.cpp)
target_link_libraries(ng_test_lua_script_library PRIVATE
    ng_module_script ng_core ng_uischema ng_foundation Qt6::Core)
set_target_properties(ng_test_lua_script_library PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_lua_script_library COMMAND ng_test_lua_script_library)

# B8-b：TransformField 输入控件输出契约验收（offscreen GUI，链 gui_transform + core）。
add_executable(ng_test_transform_field test_transform_field.cpp)
target_link_libraries(ng_test_transform_field PRIVATE
    ng_gui_transform ng_core ng_uischema_widgets Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ng_test_transform_field PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_transform_field COMMAND ng_test_transform_field)
set_tests_properties(ng_transform_field PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# === 协议逻辑资产（顺序段协议解码内核平移）===
# 纯逻辑测试：只依赖 ng_protocol + Qt Core，headless。
add_executable(ng_test_protocol test_protocol.cpp)
target_link_libraries(ng_test_protocol PRIVATE
    ng_protocol ng_ext_common ng_ext_registry ng_host ng_core Qt6::Core)
target_compile_definitions(ng_test_protocol PRIVATE
    NG_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/../tests/fixture")
set_target_properties(ng_test_protocol PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_protocol COMMAND ng_test_protocol)

# === 协议模块意图面（ng/modules/protocol 建立在 ng_protocol 之上）===
# headless：只链模块 + 库 + foundation，不拉 Widgets。
# 覆盖 4 个 op 的正常/拒绝双路：op 认领、validate 分阶段、preview 双入口、模板库往返。
add_executable(ng_test_protocol_module test_protocol_module.cpp)
target_link_libraries(ng_test_protocol_module PRIVATE
    ng_module_protocol ng_protocol ng_host ng_core ng_foundation Qt6::Core)
set_target_properties(ng_test_protocol_module PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_protocol_module COMMAND ng_test_protocol_module)
add_executable(ng_test_architecture test_architecture.cpp)
target_include_directories(ng_test_architecture PRIVATE ${CMAKE_SOURCE_DIR}/app)
# OBJECT 库必须显式链接：模块/知识包靠 TU 静态对象自注册。
target_link_libraries(ng_test_architecture PRIVATE
    ng_module_data ng_module_plot ng_module_script ng_module_protocol ng_module_analysis ng_module_settings
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    ng_ext_registry ng_host ng_core Qt6::Core)
# 复用 CLI 里的 App 装配逻辑
target_sources(ng_test_architecture PRIVATE ${CMAKE_SOURCE_DIR}/app/App.cpp)
set_target_properties(ng_test_architecture PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_architecture COMMAND ng_test_architecture)

# === 统一变换 / Selection / 能力防错契约（headless）===
# 链 ng_module_script 触发 TransformEvaluator 注入；表达式走 Rust，Lua 走 C++。
add_executable(ng_test_transform test_transform.cpp)
target_include_directories(ng_test_transform PRIVATE ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(ng_test_transform PRIVATE
    ng_module_data ng_module_plot ng_module_script ng_module_protocol ng_module_analysis ng_module_settings
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    ng_ext_registry ng_host ng_core Qt6::Core)
target_sources(ng_test_transform PRIVATE ${CMAKE_SOURCE_DIR}/app/App.cpp)
set_target_properties(ng_test_transform PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_transform COMMAND ng_test_transform)

# === GUI 契约测试 ===
# 独立可执行文件：GUI 依赖 Qt Widgets，与 headless 测试分开，
# 保证 headless（CLI）构建不被 Widgets 污染。
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(ng_test_gui test_gui.cpp)
target_include_directories(ng_test_gui PRIVATE ${CMAKE_SOURCE_DIR}/app)
# 领域面板的呈现层：领域面板的 QWidget 实现按设计只在 GUI 侧链接。
# 测试要验证"描述 + 实现共用一张工厂表"，就必须像真正的 GUI 一样链接它；
# headless 测试（ng_test_architecture）故意不链，借此守住 CLI 的 headless 纯净。
target_link_libraries(ng_test_gui PRIVATE
    ng_gui
    # 主题库：OBJECT 库之间不传播目标文件，必须显式链进可执行文件。
    ng_gui_theme
    ng_gui_transform
    ng_module_data ng_module_data_gui ng_module_plot ng_module_script ng_module_protocol ng_module_analysis ng_module_settings
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    # 领域面板呈现层（OBJECT 库）：自注册 TU 必须显式链接进来。
    ng_ext_automotive_gui
    ng_ext_registry ng_host ng_core Qt6::Widgets Qt6::Gui Qt6::Core)
target_sources(ng_test_gui PRIVATE ${CMAKE_SOURCE_DIR}/app/App.cpp)
# 测试数据目录：仓库根的 tests/fixture（ng 自带 fixture 会重复同一批样本）。
# 数据源面板的契约测试必须走**真实加载路径**（data.load 意图），
# 因为"数据集归属哪个源"只有走摄取管道才会产生——直接往仓库塞数据集测不到。
target_compile_definitions(ng_test_gui PRIVATE
    NG_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/../tests/fixture")
set_target_properties(ng_test_gui PROPERTIES AUTOMOC ON CXX_STANDARD 17)
# offscreen 平台插件：无显示环境（CI）也能跑。
add_test(NAME ng_gui COMMAND ng_test_gui)
set_tests_properties(ng_gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# === 新架构契约测试（Source 多数据源 + Parser SPI）===
# 独立可执行文件：只依赖 core + 扩展里的解析器，不需要 GUI。
add_executable(ng_test_pipeline test_pipeline.cpp)
target_include_directories(ng_test_pipeline PRIVATE
    ${CMAKE_SOURCE_DIR}/extensions/common
    ${CMAKE_SOURCE_DIR}/core/include)
target_link_libraries(ng_test_pipeline PRIVATE
    ng_recording
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    ng_ext_registry ng_host ng_core Qt6::Core)
set_target_properties(ng_test_pipeline PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_pipeline COMMAND ng_test_pipeline)

# === fastplot 绘图库契约测试 ===
# 与 ng_test_gui 同理：Widgets 侧测试单独成可执行文件，offscreen 下跑。
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
target_include_directories(fastplot_dataset_test PRIVATE ${CMAKE_SOURCE_DIR}/core/include)
target_link_libraries(fastplot_dataset_test PRIVATE
    fastplot
    ng_recording
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    ng_ext_registry ng_host ng_core Qt6::Widgets Qt6::Gui Qt6::Core)
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
add_executable(ng_test_instrument test_instrument.cpp)
target_include_directories(ng_test_instrument PRIVATE ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(ng_test_instrument PRIVATE
    ng_gui
    # 主题库：OBJECT 库之间不传播目标文件，必须显式链进可执行文件。
    ng_gui_theme
    ng_gui_transform
    ng_module_data ng_module_data_gui ng_module_plot ng_module_script ng_module_protocol ng_module_analysis ng_module_settings
    ng_recording
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    ng_ext_registry ng_host ng_core Qt6::Widgets Qt6::Gui Qt6::Core)
target_sources(ng_test_instrument PRIVATE ${CMAKE_SOURCE_DIR}/app/App.cpp)
set_target_properties(ng_test_instrument PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_instrument COMMAND ng_test_instrument)
set_tests_properties(ng_instrument PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# === 画布导出 / 视图状态 / 图例（P6：库能力接进 ng 视图层）===
add_executable(ng_test_plot_io test_plot_io.cpp)
target_include_directories(ng_test_plot_io PRIVATE ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(ng_test_plot_io PRIVATE
    ng_gui
    # 主题库：OBJECT 库之间不传播目标文件，必须显式链进可执行文件。
    ng_gui_theme
    ng_gui_transform
    ng_module_data ng_module_data_gui ng_module_plot ng_module_script ng_module_protocol ng_module_analysis ng_module_settings
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    ng_recording
    ng_ext_registry ng_host ng_core Qt6::Widgets Qt6::Gui Qt6::Core)
target_sources(ng_test_plot_io PRIVATE ${CMAKE_SOURCE_DIR}/app/App.cpp)
set_target_properties(ng_test_plot_io PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_plot_io COMMAND ng_test_plot_io)
set_tests_properties(ng_plot_io PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Windows：把 ads.dll 拷到本目录各可执行目标旁边，构建完即可直接运行/跑测试。
ng_deploy_windows_runtime_all()

