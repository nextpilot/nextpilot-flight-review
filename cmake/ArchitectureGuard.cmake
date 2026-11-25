# ArchitectureGuard.cmake —— 三条铁律的 configure 期硬守卫（A4a）。
#
# 与 LibBoundaryGuard / LayerContractGuard / ShellLinkGuard 互补：那三个查库边界、
# 层契约、壳链接面；本守卫查**源码级 include 越界与文件夹归属**，专治"纸面合规、
# 源码偷编"的破口（如 LuaEditor 曾靠 ps_context 传递偷编 #include "Dataset.h"）。
#
# 违规一律 FATAL_ERROR——编译还没开始就红，不靠人工纪律。
# 宁可漏报不可误报：先剥离注释再扫描；抽象接口头（IMessageParser.h 在 libs/parser）
# 不算业务头，只有 toolbox/registry 容器头与具体业务头才拦。

get_filename_component(_PS_ARCH_GUARD_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# 读取文件并剥离 // 与 /* */ 注释，返回扫描文本。
function(_ps_arch_strip_comments _path _out)
  file(READ "${_path}" _txt)
  string(REGEX REPLACE "//[^\n\r]*" "" _scan "${_txt}")
  string(REGEX REPLACE "/\\*[^\n\r]*\\*/" "" _scan "${_scan}")
  set(${_out} "${_scan}" PARENT_SCOPE)
endfunction()

function(_ps_arch_guard_includes)
  # 规则表：目录 glob → 禁止的 include 正则 → 说明。
  # 1) 裸壳：零业务 include（铁律1）
  # 2) 通用控件库 libs/widget：零业务 include（可给第三方复用）
  # 3) modules：不得 include toolbox/parser 容器头（铁律2）；IMessageParser.h 是抽象接口，合规
  set(_rules
    "application/gui/shell/*|ToolboxHub[.]h|裸壳禁止 include toolbox 容器头（DialogLauncherHub/能力注入）"
    "application/gui/shell/*|Dataset[.]h|裸壳禁止 include Dataset.h（业务数据）"
    "application/gui/shell/*|DatasetHub[.]h|裸壳禁止 include DatasetHub.h（业务数据）"
    "application/gui/shell/*|IMessageParser[.]h|裸壳禁止 include 解析器接口"
    "application/gui/shell/*|ParserHub[.]h|裸壳禁止 include ParserHub"
    "libs/widget/*|ToolboxHub[.]h|通用控件库禁止 include toolbox"
    "libs/widget/*|Dataset[.]h|通用控件库禁止 include Dataset.h（走能力注入采样）"
    "libs/widget/*|DatasetHub[.]h|通用控件库禁止 include DatasetHub.h"
    "application/modules/*|ToolboxHub[.]h|modules 禁止 include toolbox 容器头（走 AppContext 解析目录）"
    "application/modules/*|ParserHub[.]h|modules 禁止 include ParserHub（走 AppContext::parserIds）"
  )
  foreach(_rule IN LISTS _rules)
    string(REPLACE "|" ";" _parts "${_rule}")
    list(GET _parts 0 _glob)
    list(GET _parts 1 _rx)
    list(GET _parts 2 _why)
    file(GLOB_RECURSE _files
      "${_PS_ARCH_GUARD_ROOT}/src/${_glob}.cpp"
      "${_PS_ARCH_GUARD_ROOT}/src/${_glob}.h")
    foreach(_f IN LISTS _files)
      _ps_arch_strip_comments("${_f}" _scan)
      if(_scan MATCHES "#[ \t]*include[ \t]*[\"<][^\n\"<>]*${_rx}")
        message(FATAL_ERROR
          "[架构守卫/include] ${_f}\n  ${_why}。\n"
          "  命中禁止模式: ${_rx}")
      endif()
    endforeach()
  endforeach()
endfunction()

function(_ps_arch_guard_folder_ownership)
  # gui/shell 只允许壳自己的文件；业务对话框/业务控件一律归模块或 libs/widget。
  # 用文件名黑名单（这些类已迁出，残留即归属错位）。
  set(_forbidden_names
    "ImportDialog" "ReportExporter" "LuaEditor" "ProtocolEditorDialog"
    "ProtocolInspectorDialog" "TransformEditorDialog" "TransformField"
    "FilterEditorDialog" "TransformScriptDialog")
  file(GLOB_RECURSE _shell_files
    "${_PS_ARCH_GUARD_ROOT}/src/application/gui/shell/*.cpp"
    "${_PS_ARCH_GUARD_ROOT}/src/application/gui/shell/*.h")
  foreach(_f IN LISTS _shell_files)
    get_filename_component(_name "${_f}" NAME_WE)
    if(_name IN_LIST _forbidden_names)
      message(FATAL_ERROR
        "[架构守卫/归属] ${_f}\n  业务对话框/控件不得留在 gui/shell（裸壳零业务）。"
        "应归 modules/<name>/gui 或 libs/widget。")
    endif()
  endforeach()
endfunction()

function(ps_arch_guard_contract)
  _ps_arch_guard_includes()
  _ps_arch_guard_folder_ownership()
  message(STATUS "[架构守卫] 通过：include 黑名单 + 文件夹归属无违规")
endfunction()
