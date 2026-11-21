find_package(Qt6 REQUIRED COMPONENTS Core)

# 报告器官（headless）：只声明「报告」页的 UI 入口（纯数据 UiAction），
# 导出管线在外壳层（gui/shell/ReportExporter）。OBJECT 库保证自注册 TU 参与链接。
add_library(ng_module_report OBJECT
    ReportModule.cpp ReportModule.h)
target_include_directories(ng_module_report PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_module_report PUBLIC ng_host ng_context Qt6::Core)
set_target_properties(ng_module_report PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
