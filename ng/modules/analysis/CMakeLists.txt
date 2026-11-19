find_package(Qt6 REQUIRED COMPONENTS Core)

# 分析器官（headless）：提供 analysis.run 意图调度 + 分析页 gallery 入口（纯数据 UiAction）。
# 具体领域应用由 extensions 贡献，本模块不懂业务。OBJECT 库保证自注册 TU 参与链接。
add_library(ng_module_analysis OBJECT AnalysisModule.cpp AnalysisModule.h)
target_include_directories(ng_module_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_module_analysis PUBLIC ng_host ng_core Qt6::Core)
set_target_properties(ng_module_analysis PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
