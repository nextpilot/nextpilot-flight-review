set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(plotscope_app
  main.cpp
  shell/MainWindow.h
  shell/MainWindow.cpp
  plot/CanvasWidget.h
  plot/CanvasWidget.cpp
)

target_include_directories(plotscope_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(plotscope_app PRIVATE Qt6::Widgets ps_kernel)
ps_set_warnings(plotscope_app)

if(WIN32)
  set_target_properties(plotscope_app PROPERTIES WIN32_EXECUTABLE ON)
endif()
