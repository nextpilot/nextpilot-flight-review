# ShellLinkGuard.cmake —— 壳链接护栏（契约 §11 链接关 / 验收 6）。
#
# 壳 target（ps_gui 与裸壳 ps_shell_bare）的链接依赖**只许**是 libs 层
# （ps_host / ps_context / ps_command / ps_gui_theme / ps_gui_transform /
# ps_protocol / 其余 libs/*）、Qt 与 fastplot；不许出现任何
# ps_module_* / ps_toolbox_* / ps_parser_*。装配权上移到组合根（exe / 装配层），
# 壳只接收"已装配好的 Pack + ToolboxHub"。
#
# 护栏比纪律可靠：后来者把模块塞回壳链接面，configure 期即 FATAL_ERROR，
# 编译还没开始就红。本函数同时罩 ps_gui 与 ps_shell_bare 两个壳产物。
function(ps_guard_shell_links _target)
    foreach(_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_links ${_target} ${_prop})
        if(NOT _links)
            continue()
        endif()
        foreach(_l IN LISTS _links)
            # 跳过生成器表达式 / 绝对路径 / 系统库（与 LayerContractGuard 同口径）。
            if(_l MATCHES "^\\$" OR _l MATCHES "^/")
                continue()
            endif()
            if(_l MATCHES "^ps_module_|^ps_toolbox_|^ps_parser_")
                message(FATAL_ERROR
                    "${_target} 链了业务模块 ${_l}：壳只许依赖 libs 层 + Qt + fastplot"
                    "（契约 §11 链接关 / 验收 6），装配权归组合根。")
            endif()
        endforeach()
    endforeach()
endfunction()

# 最小 app 组装闭包护栏（契约 §11 链接关 / 验收 20）。
#
# 与 ps_guard_shell_links 的差别：那个只看壳自己的**直接**链接面，这个走
# 传递闭包——`ps_app` / `ps_shell_bare` 自己链的都是 libs，但某个 lib 一旦
# INTERFACE 链上了 ps_module_*，闭包里就悄悄混进业务代码，"换个产品只换
# modules 集合"当场失效。验收 20 要的正是"递归断言"，所以这里逐层展开。
#
# 只展开真 target 名；生成器表达式、绝对路径、系统库一律跳过（与另两个护栏同口径）。
# 递归深度靠 visited 集合兜住环形依赖。
function(_ps_walk_closure _lib _visited_var)
    if(_lib MATCHES "^ps_module_|^ps_toolbox_|^ps_parser_")
        message(FATAL_ERROR
            "最小 app 闭包里出现业务模块 ${_lib}（契约 §11 链接关 / 验收 20）："
            "模块只能由可执行文件层装配，不许从 libs 传递进来。")
    endif()
    if(_lib MATCHES "^\\$" OR _lib MATCHES "^/" OR NOT TARGET ${_lib})
        return()
    endif()
    set(_visited ${${_visited_var}})
    list(FIND _visited ${_lib} _seen)
    if(NOT _seen EQUAL -1)
        return()
    endif()
    list(APPEND _visited ${_lib})
    set(${_visited_var} ${_visited} PARENT_SCOPE)
    foreach(_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_links ${_lib} ${_prop})
        if(NOT _links)
            continue()
        endif()
        foreach(_dep IN LISTS _links)
            _ps_walk_closure(${_dep} ${_visited_var})
        endforeach()
    endforeach()
endfunction()

function(ps_guard_closure_has_no_business _target)
    set(_visited "")
    foreach(_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_links ${_target} ${_prop})
        if(NOT _links)
            continue()
        endif()
        foreach(_dep IN LISTS _links)
            _ps_walk_closure(${_dep} _visited)
        endforeach()
    endforeach()
    message(STATUS "[closure-guard] ${_target} 传递闭包无业务模块（验收 20）")
endfunction()
