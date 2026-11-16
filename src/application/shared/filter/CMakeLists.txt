add_library(ps_filter_ui STATIC
  include/filter/FilterCatalog.h
  include/filter/FilterParamEditor.h
  src/FilterParamEditor.cpp
)
set_target_properties(ps_filter_ui PROPERTIES AUTOMOC ON)
target_include_directories(ps_filter_ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ps_filter_ui PUBLIC Qt6::Widgets)
ps_set_warnings(ps_filter_ui)
