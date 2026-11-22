# Lua 5.4.4 —— 原样引入的源码（MIT，见 README-upstream.md）
#
# 只做一件事：把库本体编成静态库目标 `lua::lua`，由应用侧直接链接。
# 不编 lua.c / luac.c（那两个是命令行入口，带 main）。
#
# 源码编库：全平台吃同一份输入，产物格式由各自的编译器决定，不依赖
# 仓库外的预编译产物（deps/ 不入库，换平台如 Windows/MSVC 无法复用）。

file(GLOB LUA_LIB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
list(FILTER LUA_LIB_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")

add_library(lua STATIC ${LUA_LIB_SOURCES})
add_library(lua::lua ALIAS lua)

target_include_directories(lua PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# POSIX 平台需要 LUA_USE_LINUX：打开 dlopen（loadlib）与 readline/历史库可选路径；
# Windows 走默认的 _WIN32 分支（LoadLibrary），不需要额外定义。
if(UNIX AND NOT APPLE)
    target_compile_definitions(lua PUBLIC LUA_USE_LINUX)
endif()

set_target_properties(lua PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    # Lua 是干净 C89 代码，MSVC 的 /W4 会刷一堆无关告警
    target_compile_options(lua PRIVATE /W0)
endif()
