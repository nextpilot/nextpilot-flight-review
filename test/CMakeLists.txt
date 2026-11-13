add_executable(test_abi unit/test_abi.cpp)
target_link_libraries(test_abi PRIVATE ps_kernel)
add_test(NAME abi_selftest COMMAND test_abi)

if(TARGET ps_loader_csv)
  add_executable(test_plugin_csv unit/test_plugin_csv.cpp)
  target_include_directories(test_plugin_csv PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(test_plugin_csv PRIVATE ps_loader_csv ps_kernel)
  add_test(NAME plugin_csv COMMAND test_plugin_csv)
endif()

if(TARGET ps_loader_ulog AND TARGET ps_loader_apbin AND TARGET ps_loader_asc AND TARGET ps_loader_blf)
  add_executable(test_plugin_file_formats unit/test_plugin_file_formats.cpp)
  target_include_directories(test_plugin_file_formats PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(test_plugin_file_formats PRIVATE
    PS_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/test/fixture")
  target_link_libraries(test_plugin_file_formats PRIVATE
    ps_loader_ulog ps_loader_apbin ps_loader_asc ps_loader_blf ps_kernel)
  add_test(NAME plugin_file_formats COMMAND test_plugin_file_formats)
endif()

if(TARGET ps_runtime)
  add_library(ps_demo_plugin MODULE external_plugin/DemoPlugin.cpp)
  target_include_directories(ps_demo_plugin PRIVATE ${PROJECT_SOURCE_DIR}/src)
  set_target_properties(ps_demo_plugin PROPERTIES
    PREFIX "" OUTPUT_NAME "demo_plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins")

  add_library(ps_bad_plugin MODULE external_plugin/BadPlugin.cpp)
  target_include_directories(ps_bad_plugin PRIVATE ${PROJECT_SOURCE_DIR}/src)
  set_target_properties(ps_bad_plugin PROPERTIES
    PREFIX "" OUTPUT_NAME "bad_plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins")

  add_executable(test_plugin_runtime unit/test_plugin_runtime.cpp)
  target_include_directories(test_plugin_runtime PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(test_plugin_runtime PRIVATE
    PS_PLUGIN_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/plugins")
  target_link_libraries(test_plugin_runtime PRIVATE ps_runtime)
  add_dependencies(test_plugin_runtime ps_demo_plugin ps_bad_plugin)
  add_test(NAME plugin_runtime COMMAND test_plugin_runtime)
  file(COPY ${PROJECT_SOURCE_DIR}/test/external_plugin/manifest.json
       ${PROJECT_SOURCE_DIR}/test/external_plugin/dialog.ui
       DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/plugins)
endif()

if(PS_CARGO)
  add_test(NAME kernel_cargo_test
           COMMAND "${PS_CARGO}" test --manifest-path "${PROJECT_SOURCE_DIR}/src/kernel/Cargo.toml"
           WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/src/kernel")
endif()
