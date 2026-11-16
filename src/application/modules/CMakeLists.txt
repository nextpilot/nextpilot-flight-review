add_subdirectory(datasource)
add_subdirectory(plotting)

# 协议：协议文档、编辑器、codec 与自定义协议 loader 归模块，不再走内置扩展聚合。
if(PS_BUILD_CUSTOM_PROTOCOL)
  add_subdirectory(protocol)
endif()
