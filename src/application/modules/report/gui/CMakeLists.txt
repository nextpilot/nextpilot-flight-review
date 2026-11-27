find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)

# report 功能的**呈现层**：PDF 导出管线（QPdfWriter + 画布矢量渲染）。
#
# 与 headless 器官（ps_module_report）分开：器官是纯 UiAction 声明（CLI 也链），
# 呈现层含 Qt Gui/Widgets，只能被 GUI 可执行文件链接。report.export 由
# ReportExportHost 实现 IReportExportHost，组合根注册、report 模块按生命周期安装（D18a）。
add_library(ps_module_report_gui OBJECT
    ReportExporter.cpp
    ReportExporter.h
)
target_include_directories(ps_module_report_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # IReportExportHost.h（模块本体零 Qt Widgets）
)
target_link_libraries(ps_module_report_gui PUBLIC
    ps_context ps_registry ps_parser ps_dataset ps_uischema Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_module_report_gui PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
