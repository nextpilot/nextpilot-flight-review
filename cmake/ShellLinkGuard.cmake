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
