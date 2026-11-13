add_executable(test_abi unit/test_abi.cpp)
target_link_libraries(test_abi PRIVATE ps_kernel)
add_test(NAME abi_selftest COMMAND test_abi)

if(TARGET ps_loader_csv)
  add_executable(test_plugin_csv unit/test_plugin_csv.cpp)
  target_include_directories(test_plugin_csv PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(test_plugin_csv PRIVATE ps_loader_csv ps_kernel)
  add_test(NAME plugin_csv COMMAND test_plugin_csv)
endif()

if(PS_CARGO)
  add_test(NAME kernel_cargo_test
           COMMAND "${PS_CARGO}" test --manifest-path "${PROJECT_SOURCE_DIR}/src/kernel/Cargo.toml"
           WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/src/kernel")
endif()
