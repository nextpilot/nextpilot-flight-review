# 运行期依赖部署（Windows 专用）
#
# ADS（Qt-Advanced-Docking-System）在 Windows 上是动态库：exe 在 gui/Release、
# 测试在 tests/Release，而 ads.dll 在 3rdparty/ads/Release。缺少它时 ctest 直接
# 报 0xc0000135（STATUS_DLL_NOT_FOUND），必须手工往 PATH 里塞目录才能跑。
#
# 这里在构建后把 ads.dll 拷到每个可执行目标自己的输出目录，让"构建完就能跑"
# 成立，不依赖任何环境变量。非 Windows 平台 ADS 是静态库（无 dll），函数直接空转。

function(ng_deploy_windows_runtime target)
    if(NOT WIN32)
        return()
    endif()
    if(NOT TARGET ads)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:ads>"
                "$<TARGET_FILE_DIR:${target}>"
        COMMENT "部署 ads.dll -> $<TARGET_FILE_DIR:${target}>")
endfunction()

# 把当前目录里已定义的可执行目标全部挂上部署步骤，省得逐个列名。
# 必须在目标定义之后调用（通常放在 CMakeLists 末尾）。
function(ng_deploy_windows_runtime_all)
    if(NOT WIN32)
        return()
    endif()
    get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "EXECUTABLE")
            ng_deploy_windows_runtime(${_t})
        endif()
    endforeach()
endfunction()
