find_package(Qt6 REQUIRED COMPONENTS Core)

# 一个功能 = 一个目录：器官（意图/事件）+ 它的专属视图物理上放在一起，
# 方便作为一个完整单元开发、查找、编辑。二者靠 CMake 拆成两个 target：
#
#   ps_module_data      —— headless 器官（DataModule），只链 Qt Core，CLI 装载
#   ps_module_data_gui  —— 功能专属视图（DataManagerPanel），链 Widgets，只 GUI 装载
#
# 这样"物理聚合 + 编译期分离"同时成立：开发时在一个目录看全功能，
# 编译时 CLI 只拿 headless 那一半，不碰 Widgets。

# OBJECT 库：自注册依赖 TU 静态对象的构造函数；静态库会丢弃未被引用的
# 目标文件，导致注册静默失效（见 AGENTS.md 硬线 #6）。
add_library(ps_module_data OBJECT DataModule.cpp DataModule.h)
target_include_directories(ps_module_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_module_data PUBLIC ps_host)
set_target_properties(ps_module_data PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

# 表格查看模型（2.2.2）：纯 QtCore，懒加载分页。独立静态库——
# ps_module_data_gui 链它出视图，headless 测试（ps_test_data_table）也只链它，
# 不必拖 Widgets。无自注册，STATIC 不受硬线 #6 约束。
add_library(ps_data_tablemodel STATIC DataTableModel.cpp DataTableModel.h)
target_include_directories(ps_data_tablemodel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_data_tablemodel PUBLIC ps_dataset Qt6::Core)
set_target_properties(ps_data_tablemodel PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

# 呈现层（领域专属面板的 QWidget 实现）：单独一个产物，只被 GUI 链接。
# 见 gui/CMakeLists.txt 的说明。
add_subdirectory(gui)
