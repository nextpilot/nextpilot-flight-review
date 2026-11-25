# 格式自动化：只格式化"变更行"，不做整文件重排。
#
# 为什么不是 `clang-format -i` 全树：见仓库根 .clang-format 头注释——现存代码是
# 手工排版，整文件重排会改写约 15% 的行、毁掉 git blame，明确 out of scope。
# 所以这里走 git clang-format，它只动"相对基线有差异的那些行"。
#
# 用法（在已有 build 目录上跑，不需要重新 configure）：
#   cmake --build build --target format
#
# 换基线（默认 origin/main；修历史提交的格式时很有用）：
#   cmake -S . -B build -DPS_FORMAT_BASE=dda1f8c9
#
# CI 不做自动改写：.github/workflows/lint.yml 的 format job 只检查不修复，
# 否则门禁会先把自己的输入改干净，永远绿。

set(PS_FORMAT_BASE "origin/main" CACHE STRING
    "git clang-format 的基准 ref 或提交；只格式化相对它变更的行")

find_program(PS_CLANG_FORMAT_BIN NAMES clang-format)
find_program(PS_GIT_CLANG_FORMAT NAMES git-clang-format)

if(PS_CLANG_FORMAT_BIN AND PS_GIT_CLANG_FORMAT)
  add_custom_target(format
    COMMAND ${PS_GIT_CLANG_FORMAT} --binary ${PS_CLANG_FORMAT_BIN} ${PS_FORMAT_BASE}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    VERBATIM
    COMMENT "clang-format: 仅格式化相对 ${PS_FORMAT_BASE} 的变更行")
  message(STATUS "format target 可用：cmake --build <build> --target format（基线 ${PS_FORMAT_BASE}）")
else()
  message(STATUS "format target 不可用：找不到 clang-format 或 git-clang-format（CI 的 format job 仍会检查）")
endif()
