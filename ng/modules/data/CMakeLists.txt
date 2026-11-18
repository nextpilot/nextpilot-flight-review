find_package(Qt6 REQUIRED COMPONENTS Core)

# 一个功能 = 一个目录：器官（意图/事件）+ 它的专属视图物理上放在一起，
# 方便作为一个完整单元开发、查找、编辑。二者靠 CMake 拆成两个 target：
#
#   ng_module_data      —— headless 器官（DataModule），只链 Qt Core，CLI 装载
#   ng_module_data_gui  —— 功能专属视图（DataManagerPanel），链 Widgets，只 GUI 装载
#
# 这样"物理聚合 + 编译期分离"同时成立：开发时在一个目录看全功能，
# 编译时 CLI 只拿 headless 那一半，不碰 Widgets。

# OBJECT 库：自注册依赖 TU 静态对象的构造函数；静态库会丢弃未被引用的
# 目标文件，导致注册静默失效（见 AGENTS.md 硬线 #6）。
add_library(ng_module_data OBJECT DataModule.cpp DataModule.h)
target_include_directories(ng_module_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_module_data PUBLIC ng_host)
set_target_properties(ng_module_data PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

# 功能专属视图：聚合在本功能目录，但独立 target、只被 GUI 可执行文件链接。
# DataManagerPanel 通过 NG_REGISTER_PANEL("dataset-tree") 自注册到 core 的工厂表，
# 与器官解耦（器官只发字符串 id，不 include 本头文件）。
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)
add_library(ng_module_data_gui OBJECT DataManagerPanel.cpp DataManagerPanel.h)
target_include_directories(ng_module_data_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_module_data_gui PUBLIC ng_core Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ng_module_data_gui PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
