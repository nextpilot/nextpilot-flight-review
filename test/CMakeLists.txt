add_executable(test_abi unit/test_abi.cpp)
target_link_libraries(test_abi PRIVATE ps_kernel)
add_test(NAME abi_selftest COMMAND test_abi)

if(PS_CARGO)
  add_test(NAME kernel_cargo_test
           COMMAND "${PS_CARGO}" test --manifest-path "${PROJECT_SOURCE_DIR}/src/kernel/Cargo.toml"
           WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/src/kernel")
endif()
