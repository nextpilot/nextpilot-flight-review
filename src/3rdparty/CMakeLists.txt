# 第三方库集中放这里：每个库一个子目录，构建时只编被链接到的目标。
# 约定：进这里之前先核对许可（D12），并在目录内保留上游 LICENSE。

# SARibbon —— Ribbon 结构层（MIT，2.9.6，vendored 源码便于内网/离线构建）
set(SARIBBON_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(SARIBBON_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS               OFF CACHE BOOL "" FORCE)
set(SARIBBON_USE_FRAMELESS_LIB OFF CACHE BOOL "" FORCE)
set(SARIBBON_INSTALL_IN_CURRENT_DIR OFF CACHE BOOL "" FORCE)
add_subdirectory(saribbon EXCLUDE_FROM_ALL)
