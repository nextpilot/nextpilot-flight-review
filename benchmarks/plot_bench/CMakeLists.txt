if(NOT TARGET ps_application_shell OR NOT TARGET ps_kernel)
  return()
endif()
find_package(Qt6 REQUIRED COMPONENTS Widgets Network PrintSupport)
find_package(Qt6 QUIET COMPONENTS Charts)

set(PS_ROOT ${PROJECT_SOURCE_DIR})
add_executable(plot_bench main.cpp ${PS_ROOT}/src/application/modules/plotting/widget/canvas/PlottingCanvas.cpp)
set_target_properties(plot_bench PROPERTIES AUTOMOC ON)
target_include_directories(plot_bench PRIVATE
    ${PS_ROOT}/src
    ${PS_ROOT}/src/application
    ${PS_ROOT}/src/application/runtime/host/include
    ${PS_ROOT}/src/application/modules/plotting/widget/canvas
    ${PS_ROOT}/src/application/shell
    ${PS_ROOT}/benchmarks/qcustomplot-source
    ${PS_ROOT}/benchmarks/qwt-6.3.0/src)
target_link_libraries(plot_bench PRIVATE ps_application_shell ps_kernel
    Qt6::Widgets Qt6::Network Qt6::PrintSupport)
target_compile_definitions(plot_bench PRIVATE HAVE_QCUSTOMPLOT)
target_sources(plot_bench PRIVATE ${PS_ROOT}/benchmarks/qcustomplot-source/qcustomplot.cpp)
if(Qt6Charts_FOUND)
    target_compile_definitions(plot_bench PRIVATE HAVE_QTCHARTS)
    target_link_libraries(plot_bench PRIVATE Qt6::Charts)
endif()
if(EXISTS ${PS_ROOT}/benchmarks/qwt-build/lib/libqwt.so)
    target_compile_definitions(plot_bench PRIVATE HAVE_QWT)
    target_link_libraries(plot_bench PRIVATE ${PS_ROOT}/benchmarks/qwt-build/lib/libqwt.so)
    set_target_properties(plot_bench PROPERTIES BUILD_RPATH ${PS_ROOT}/benchmarks/qwt-build/lib)
endif()
if(EXISTS ${PS_ROOT}/benchmarks/jkqt-install/lib/cmake/JKQTPlotter6/JKQTPlotter6Config.cmake)
    list(APPEND CMAKE_PREFIX_PATH ${PS_ROOT}/benchmarks/jkqt-install)
    find_package(JKQTPlotter6 QUIET CONFIG)
    if(JKQTPlotter6_FOUND)
        target_compile_definitions(plot_bench PRIVATE HAVE_JKQT)
        target_link_libraries(plot_bench PRIVATE JKQTPlotter6::JKQTPlotter6)
        set_target_properties(plot_bench PROPERTIES BUILD_RPATH ${PS_ROOT}/benchmarks/jkqt-install/lib)
    endif()
endif()
