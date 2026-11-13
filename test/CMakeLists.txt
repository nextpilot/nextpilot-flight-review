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

if(PS_CARGO)
  add_test(NAME kernel_cargo_test
           COMMAND "${PS_CARGO}" test --manifest-path "${PROJECT_SOURCE_DIR}/src/kernel/Cargo.toml"
           WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/src/kernel")
endif()
