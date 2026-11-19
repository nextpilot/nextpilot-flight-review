# LayerContractGuard.cmake —— configure 期扫描全层依赖方向，违规直接报错。
#
# 契约真源是 ng/LAYERS.md。本守卫只做静态可判定的两件事，宁可漏报不可误报：
#   1. 层级倒挂：上层依赖下层是合法方向，下层依赖上层 FATAL
#   2. 同层横向：module↔module、ext↔module、ext↔ext FATAL
# libs 的另外三条破口（逃逸 include / 单例 / 上层链接）由 LibBoundaryGuard 负责。

get_filename_component(_NG_LAYER_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# 依赖层映射（按 target 名，不按物理目录——共享 UI 库虽在 gui/ 但归 L3）。
# L0 foundation / L1 机制库 / L2 core / L3 host+共享UI / L4 module+ext / L5 gui / L6 可执行
function(_ng_layer_of _target _out)
  set(_layer "")
  if(_target STREQUAL "ng_foundation" OR _target STREQUAL "ng_kernel")
    set(_layer 0)
  elseif(_target STREQUAL "ng_dataset" OR _target STREQUAL "ng_registry" OR
         _target STREQUAL "ng_intent" OR _target STREQUAL "ng_uischema" OR
         _target STREQUAL "ng_uischema_widgets" OR _target STREQUAL "ng_protocol" OR
         _target STREQUAL "fastplot" OR _target STREQUAL "ng_lua")
    set(_layer 1)
  elseif(_target STREQUAL "ng_core")
    set(_layer 2)
  elseif(_target STREQUAL "ng_host" OR _target STREQUAL "ng_recording" OR
         _target STREQUAL "ng_gui_theme" OR _target STREQUAL "ng_gui_transform")
    set(_layer 3)
  elseif(_target MATCHES "^ng_module_" OR _target MATCHES "^ng_ext_")
    set(_layer 4)
  elseif(_target STREQUAL "ng_gui")
    set(_layer 5)
  elseif(_target MATCHES "^plotscope")
    set(_layer 6)
  endif()
  set(${_out} "${_layer}" PARENT_SCOPE)
endfunction()

function(_ng_layer_guard_targets)
  # 收集 ng 下所有 add_library/add_executable 的 target 名（排除构建目录）。
  file(GLOB_RECURSE _cmakes "${_NG_LAYER_ROOT}/*/CMakeLists.txt")
  set(_targets "")
  foreach(_c IN LISTS _cmakes)
    if(_c MATCHES "/build[^/]*/")
      continue()
    endif()
    file(READ "${_c}" _txt)
    string(REGEX REPLACE "//[^\n\r]*" "" _txt "${_txt}")
    string(REGEX MATCHALL "(add_library|add_executable)[ \t]*\\([ \t]*[A-Za-z0-9_]+" _hits "${_txt}")
    foreach(_h IN LISTS _hits)
      string(REGEX REPLACE "(add_library|add_executable)[ \t]*\\([ \t]*" "" _name "${_h}")
      list(APPEND _targets "${_name}")
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES _targets)

  set(_violations "")

  foreach(_t IN LISTS _targets)
    _ng_layer_of("${_t}" _from)
    if(_from STREQUAL "" OR NOT TARGET ${_t})
      continue()
    endif()

    foreach(_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(_links ${_t} ${_prop})
      if(NOT _links)
        continue()
      endif()
      foreach(_l IN LISTS _links)
        # 跳过非本项目 target：Qt / 第三方 / generator 表达式 / 系统库。
        if(_l MATCHES "::" OR _l MATCHES "^\\$" OR _l MATCHES "^/" OR
           _l MATCHES "^m$" OR _l MATCHES "^[a-z]+$")
          continue()
        endif()
        _ng_layer_of("${_l}" _to)
        if(_to STREQUAL "")
          continue()
        endif()

        # 规则 2：L4 同层横向禁止（模块/知识包互不依赖）。
        if(_from EQUAL 4 AND _to EQUAL 4)
          list(APPEND _violations
            "  ${_t}(L4) → ${_l}(L4)：同层横向依赖禁止，器官/知识包之间只经 host 协作")
        endif()

        # 规则 1：层级倒挂——依赖只能向下（to 层号必须小于 from）。
        if(_to GREATER _from)
          list(APPEND _violations
            "  ${_t}(L${_from}) → ${_l}(L${_to})：下层依赖上层，方向倒挂")
        endif()
      endforeach()
    endforeach()
  endforeach()

  if(_violations)
    list(REMOVE_DUPLICATES _violations)
    string(REPLACE ";" "\n" _msg "${_violations}")
    message(FATAL_ERROR
      "[层契约] 检测到跨层依赖违规：\n${_msg}\n"
      "  层定义与允许方向见 ng/LAYERS.md。依赖只能自上而下，同层禁止横向。")
  endif()
endfunction()

function(ng_layer_guard_contract)
  _ng_layer_guard_targets()
  message(STATUS "[层契约] 守卫通过：无跨层倒挂、无同层横向依赖")
endfunction()
