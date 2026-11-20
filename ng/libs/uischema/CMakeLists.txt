find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# 契约层：ParamSpec / SchemaEngine / FormSchema，纯 metadata + 编译，headless。
add_library(ng_uischema STATIC
    src/SchemaEngine.cpp
    src/FormSchema.cpp
    src/Selection.cpp
    src/Transform.cpp
    src/PropertySchema.cpp
    include/ParamSpec.h
    include/FormSchema.h
    include/SchemaEngine.h
    include/Selection.h
    include/Transform.h
    include/FilterOps.h
    include/MarkerOps.h
    include/PropertySchema.h
)
target_include_directories(ng_uischema PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_uischema PUBLIC Qt6::Core ng_foundation ng_registry)
set_target_properties(ng_uischema PROPERTIES CXX_STANDARD 17 AUTOMOC ON)

# 渲染层：唯一的 FieldType→QWidget 映射，只 GUI 链接。
add_library(ng_uischema_widgets STATIC
    src/QtFieldRenderer.cpp
    include/QtFieldRenderer.h
)
target_include_directories(ng_uischema_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_uischema_widgets PUBLIC ng_uischema Qt6::Widgets)
set_target_properties(ng_uischema_widgets PROPERTIES CXX_STANDARD 17 AUTOMOC ON)
