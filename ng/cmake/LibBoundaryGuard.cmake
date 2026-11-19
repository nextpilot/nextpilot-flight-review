# LibBoundaryGuard.cmake —— 把 AGENTS.md §5 / master-plan §7.3 的 libs 铁律
# 变成 CMake configure 期的硬失败（编译还没开始就红）。
#
# 设计哲学：宁可漏报，不可误报。误报会逼开发者加豁免，守卫就形同虚设。
# 只检测三类能静态判定、零/低误报、且真实会发生的破口：
#   1. libs 源码 #include "../" 逃出库目录（物理隔离破口）
#   2. libs target 链接上层 target（依赖倒挂：core/host/gui/app/modules/extensions）
#   3. Meyers 单例 static T& instance()（铁律禁止的全局活实例）
# 另：namespace core 残留只发 WARNING——B7 core 消亡收口前是合法中间态。
#
# 不检测的（交给 code review / ctest，正则不可靠）：
#   - 命名空间级普通全局对象实例（易与 extern 声明/类型定义混淆）
#   - 服务定位器式 ctx.xxx() 取用（属调用习惯，非文本可判）
#   - static 自注册对象（铁律明确允许，故意不拦）

get_filename_component(_NG_LIB_GUARD_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(_ng_lib_guard_sources)
  file(GLOB_RECURSE _srcs
    "${_NG_LIB_GUARD_ROOT}/libs/*.h"
    "${_NG_LIB_GUARD_ROOT}/libs/*.cpp")
  foreach(_f IN LISTS _srcs)
    file(READ "${_f}" _txt)

    # 破口 1：相对路径逃逸。libs 只能 include 自己的 public include 与 Qt/系统头。
    if(_txt MATCHES "#[ \t]*include[ \t]*\"\\.\\./")
      message(FATAL_ERROR
        "[libs 边界] ${_f}\n"
        "  libs 内禁止 #include \"../\" 逃出库目录。跨库/上层引用必须走公开 include 路径，"
        "否则物理隔离失效（AGENTS §5.2 / master-plan §7.3）。")
    endif()

    # 破口 3：Meyers 单例——铁律禁止的全局活实例。
    if(_txt MATCHES "static[ \t]+[A-Za-z_][A-Za-z0-9_:<>, \t]*&[ \t]*instance[ \t]*\\(")
      message(FATAL_ERROR
        "[libs 边界] ${_f}\n"
        "  检测到 Meyers 单例 static T& instance()。libs 铁律禁止全局活实例/单例；"
        "请改用纯函数，或把实例交给上层组合根持有（master-plan §7.3）。")
    endif()

    # 提示：core 命名空间残留。B7 收口前是合法中间态，故只警告不失败。
    if(_txt MATCHES "namespace[ \t]+nextpilot::plotscope::core")
      message(WARNING
        "[libs 边界] ${_f} 仍用 nextpilot::plotscope::core 命名空间，"
        "应随 B7 core 消亡收口为所属库名（master-plan §7.7）。")
    endif()
  endforeach()
endfunction()

function(_ng_lib_guard_targets)
  # 上层靶标：libs 一旦链接这些就构成依赖倒挂或成环。
  set(_upper_rx "ng_core|ng_host|ng_gui|plotscope-ng|ng_module_|ng_ext_|ng_recording")

  # 自动收集 libs 下所有 add_library(NAME ...) 的 target 名——新增库无需登记。
  file(GLOB_RECURSE _cmakes "${_NG_LIB_GUARD_ROOT}/libs/*/CMakeLists.txt")
  set(_lib_targets "")
  foreach(_c IN LISTS _cmakes)
    file(READ "${_c}" _txt)
    string(REGEX MATCHALL "add_library[ \t]*\\([ \t]*[A-Za-z0-9_]+" _hits "${_txt}")
    foreach(_h IN LISTS _hits)
      string(REGEX REPLACE "add_library[ \t]*\\([ \t]*" "" _name "${_h}")
      list(APPEND _lib_targets "${_name}")
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES _lib_targets)

  foreach(_t IN LISTS _lib_targets)
    if(NOT TARGET ${_t})
      continue()
    endif()
    foreach(_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(_links ${_t} ${_prop})
      if(_links)
        foreach(_l IN LISTS _links)
          if(_l MATCHES "${_upper_rx}")
            message(FATAL_ERROR
              "[libs 边界] target '${_t}' 通过 ${_prop} 链接上层 '${_l}'。\n"
              "  libs 只能向下依赖 foundation / Qt / 系统库，"
              "禁止依赖 core / host / gui / modules / app / extensions（master-plan §7.5）。")
          endif()
        endforeach()
      endif()
    endforeach()
  endforeach()
endfunction()

function(ng_lib_guard_boundary)
  _ng_lib_guard_sources()
  _ng_lib_guard_targets()
  message(STATUS "[libs 边界] 守卫通过：无逃逸 include / 单例 / 上层依赖")
endfunction()
