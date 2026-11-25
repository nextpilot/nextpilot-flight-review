# ps_widgets —— 通用 UI 控件库（跨模块复用的"控件级"底座）。
#
# 归属判定只看**直接复用面**：被 ≥2 个模块直接复用的纯控件才进这里。
#   - TransformEditorDialog / TransformField / FilterEditorDialog：data / plot / protocol 共用
#   - TransformScriptDialog：TransformField 的配套弹窗（变换脚本配置，非通用 IDE；
#     通用脚本编辑器是 script 模块的 ScriptPanels 四件套，不在此库）
#
# 依赖只许 libs 层 + Qt；禁止 dataset / toolbox / parser / 任何 module。
#   - 取活动通道数据走 AppContext.interactions() 注入的 sampler 回调（数据模块提供），
#     控件不 include 任何 dataset 头，因此可被任意产品/第三方复用。
#   - 变换求值/脚本库/协议目录同理，全部走 AppContext 能力回调。
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

add_library(ps_widgets OBJECT
    TransformEditorDialog.cpp
    TransformEditorDialog.h
    TransformField.cpp
    TransformField.h
    FilterEditorDialog.cpp
    FilterEditorDialog.h
    TransformScriptDialog.cpp
    TransformScriptDialog.h
)
target_include_directories(ps_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_widgets PUBLIC
    ps_context ps_uischema ps_uischema_widgets ps_registry
    Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_widgets PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
