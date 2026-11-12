# 统一告警口径：MSVC /W4，GCC-Clang -Wall -Wextra，且不把三方库拉进来。
function(ps_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
endfunction()
