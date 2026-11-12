# 安装布局：bin + lib + plugins（docs 不进安装包，见 §12 目录规则）。
install(TARGETS plotscope_app RUNTIME DESTINATION bin OPTIONAL)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/src/function/"
        DESTINATION share/plotscope/function
        PATTERN "*.lua" 
        PATTERN "*.yaml")
install(DIRECTORY "${PS_PLUGIN_DIR}/"
        DESTINATION lib/plotscope/plugins
        PATTERN "CMakeLists.txt" EXCLUDE
        PATTERN "*.cpp" EXCLUDE
        PATTERN "*.h" EXCLUDE)
