find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# 契约层：ParamSpec / SchemaEngine / FormSchema，纯 metadata + 编译，headless。
add_library(ps_uischema STATIC
    src/SchemaEngine.cpp
    src/FormSchema.cpp
    src/Selection.cpp
    src/Transform.cpp
    src/PropertySchema.cpp
    src/ThemeTokens.cpp
    include/ParamSpec.h
    include/FormSchema.h
    include/SchemaEngine.h
    include/Selection.h
    include/Transform.h
    include/FilterOps.h
    include/MarkerOps.h
    include/PropertySchema.h
    include/ThemeTokens.h
)
target_include_directories(ps_uischema PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# ThemeTokens 的 QColor 属 QtGui 值类型（口径同 ps_command 的 QKeySequence），
# 不是 Widgets，headless 亦可链；本目标 PUBLIC 链里不得出现 Qt6::Widgets。
target_link_libraries(ps_uischema PUBLIC Qt6::Core Qt6::Gui ps_foundation ps_registry)
set_target_properties(ps_uischema PROPERTIES CXX_STANDARD 17 AUTOMOC ON)

# 渲染层：唯一的 FieldType→QWidget 映射，只 GUI 链接。
add_library(ps_uischema_widgets STATIC
    src/QtFieldRenderer.cpp
    include/QtFieldRenderer.h
)
target_include_directories(ps_uischema_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ps_uischema_widgets PUBLIC ps_uischema Qt6::Widgets)
set_target_properties(ps_uischema_widgets PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
