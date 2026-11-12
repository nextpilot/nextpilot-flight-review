# 版本号单一来源：Git 描述优先，退化到 project() 的版本。
find_package(Git QUIET)
set(PS_VERSION "${PROJECT_VERSION}")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE _git_desc
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(_git_desc)
    set(PS_GIT_DESC "${_git_desc}")
  endif()
endif()
message(STATUS "PlotScope ${PS_VERSION}${PS_GIT_DESC}")
