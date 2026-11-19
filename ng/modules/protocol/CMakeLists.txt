find_package(Qt6 REQUIRED COMPONENTS Core)

# 协议器官（headless）：只声明协议页的 UI 入口（纯数据 UiAction），
# 对话框由外壳按 id 特判，核心逻辑在 ng/libs/protocol。OBJECT 库保证自注册 TU 参与链接。
add_library(ng_module_protocol OBJECT ProtocolModule.cpp ProtocolModule.h)
target_include_directories(ng_module_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_module_protocol PUBLIC ng_host ng_context Qt6::Core)
set_target_properties(ng_module_protocol PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
