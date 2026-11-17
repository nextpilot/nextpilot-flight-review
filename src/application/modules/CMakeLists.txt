add_subdirectory(datasource)
add_subdirectory(analysis)
add_subdirectory(plotting)

# 脚本子系统：控制台 + 自动化 + 受限 require，见 docs/roadmap/script-automation.md
add_subdirectory(scripting)

# 协议：协议文档、编辑器、codec 与自定义协议 loader 归模块，不再走内置扩展聚合。
if(PS_BUILD_CUSTOM_PROTOCOL)
  add_subdirectory(protocol)
endif()
