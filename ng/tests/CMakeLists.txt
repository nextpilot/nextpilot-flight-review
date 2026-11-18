find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(ng_test_architecture test_architecture.cpp)
target_include_directories(ng_test_architecture PRIVATE ${CMAKE_SOURCE_DIR}/app)
# OBJECT 库必须显式链接：模块/知识包靠 TU 静态对象自注册。
target_link_libraries(ng_test_architecture PRIVATE
    ng_module_data ng_module_plot ng_module_script ng_module_settings
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    ng_ext_registry ng_host ng_core Qt6::Core)
# 复用 CLI 里的 App 装配逻辑
target_sources(ng_test_architecture PRIVATE ${CMAKE_SOURCE_DIR}/app/App.cpp)
set_target_properties(ng_test_architecture PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_architecture COMMAND ng_test_architecture)

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
    ng_module_data ng_module_data_gui ng_module_plot ng_module_script ng_module_settings
    ng_ext_common ng_ext_autopilot ng_ext_automotive ng_ext_robotics ng_ext_gnss
    # 领域面板呈现层（OBJECT 库）：自注册 TU 必须显式链接进来。
    ng_ext_automotive_gui
    ng_ext_registry ng_host ng_core Qt6::Widgets Qt6::Gui Qt6::Core)
target_sources(ng_test_gui PRIVATE ${CMAKE_SOURCE_DIR}/app/App.cpp)
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
