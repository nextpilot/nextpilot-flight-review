# 安装布局：bin + lib + extensions（docs 不进安装包，见 §12 目录规则）。
install(TARGETS plotscope_app RUNTIME DESTINATION bin OPTIONAL)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/src/base/resources/scripts/"
        DESTINATION share/plotscope/scripts
        PATTERN "*.lua" 
        PATTERN "*.yaml")
install(DIRECTORY "${PS_EXTENSION_DIR}/"
        DESTINATION lib/plotscope/extensions
        PATTERN "CMakeLists.txt" EXCLUDE
        PATTERN "*.cpp" EXCLUDE
        PATTERN "*.h" EXCLUDE)
