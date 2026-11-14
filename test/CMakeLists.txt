add_executable(test_abi unit/test_abi.cpp)
target_link_libraries(test_abi PRIVATE ps_kernel)
add_test(NAME abi_selftest COMMAND test_abi)

if(EXISTS ${PROJECT_SOURCE_DIR}/src/plugin/sdk/cpp/PsDataInputBase.hpp)
  add_executable(test_data_input_sdk unit/test_data_input_sdk.cpp)
  set_target_properties(test_data_input_sdk PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_include_directories(test_data_input_sdk PRIVATE
    ${PROJECT_SOURCE_DIR}/src/plugin/include
    ${PROJECT_SOURCE_DIR}/src/plugin/sdk/cpp)
  add_test(NAME data_input_sdk COMMAND test_data_input_sdk)
endif()

if(TARGET ps_data_loader_csv)
  add_executable(test_plugin_csv unit/test_plugin_csv.cpp)
  target_include_directories(test_plugin_csv PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/plugin/include)
  target_link_libraries(test_plugin_csv PRIVATE ps_data_loader_csv ps_kernel ps_plugin_host)
  add_test(NAME plugin_csv COMMAND test_plugin_csv)
endif()

if(TARGET ps_cache)
  add_executable(test_dataset_cache unit/test_dataset_cache.cpp)
  target_include_directories(test_dataset_cache PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/plugin/include)
  target_link_libraries(test_dataset_cache PRIVATE ps_cache ps_kernel)
  add_test(NAME dataset_cache COMMAND test_dataset_cache)
endif()

if(TARGET ps_data_loader_ulog AND TARGET ps_data_loader_apbin AND TARGET ps_data_loader_asc AND TARGET ps_data_loader_blf)
  add_executable(test_plugin_file_formats unit/test_plugin_file_formats.cpp)
  target_include_directories(test_plugin_file_formats PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/plugin/include)
  target_compile_definitions(test_plugin_file_formats PRIVATE
    PS_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/test/fixture")
  target_link_libraries(test_plugin_file_formats PRIVATE ps_plugin_host
    ps_data_loader_ulog ps_data_loader_apbin ps_data_loader_asc ps_data_loader_blf ps_data_loader_nmea ps_data_loader_ubx ps_data_loader_json ps_data_loader_binary ps_data_loader_mdf4 ps_data_loader_frame ps_data_loader_rosbag2 ps_kernel)
  add_test(NAME plugin_file_formats COMMAND test_plugin_file_formats)
endif()

if(TARGET ps_loader_common)
  add_executable(test_raw_capture unit/test_raw_capture.cpp)
  target_include_directories(test_raw_capture PRIVATE
    ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/common)
  target_link_libraries(test_raw_capture PRIVATE ps_loader_common)
  add_test(NAME raw_capture COMMAND test_raw_capture)
endif()

if(TARGET ps_data_load_mcap)
  add_executable(test_mcap_loader unit/test_mcap_loader.cpp)
  target_include_directories(test_mcap_loader PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/plugin/include)
  target_link_libraries(test_mcap_loader PRIVATE ps_data_load_mcap ps_plugin_host ps_kernel)
  add_test(NAME mcap_loader COMMAND test_mcap_loader)
endif()

if(TARGET ps_live AND TARGET ps_data_load_mcap)
  add_executable(test_stream_recorder unit/test_stream_recorder.cpp)
  target_include_directories(test_stream_recorder PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/plugin/include
    ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/common
    ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/mcap)
  target_link_libraries(test_stream_recorder PRIVATE ps_live ps_data_load_mcap)
  add_test(NAME stream_recorder COMMAND test_stream_recorder)
endif()

if(TARGET ps_data_source_mavlink)
  find_package(Qt6 REQUIRED COMPONENTS Network)
  add_executable(test_mavlink_source unit/test_mavlink_source.cpp)
  target_include_directories(test_mavlink_source PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/plugin/include)
  target_link_libraries(test_mavlink_source PRIVATE ps_data_source_mavlink ps_plugin_host ps_kernel Qt6::Network)
  add_test(NAME mavlink_source COMMAND test_mavlink_source)
endif()

if(TARGET ps_data_source_oem7)
  find_package(Qt6 REQUIRED COMPONENTS Network)
  add_executable(test_oem7_source unit/test_oem7_source.cpp)
  target_include_directories(test_oem7_source PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/plugin/include)
  target_link_libraries(test_oem7_source PRIVATE ps_data_source_oem7 ps_plugin_host ps_kernel Qt6::Network)
  target_compile_definitions(test_oem7_source PRIVATE
    TEST_OEM7_DB="${CMAKE_SOURCE_DIR}/src/plugin/data_source/oem7/oem7_database.json")
  add_test(NAME oem7_source COMMAND test_oem7_source)
endif()

if(TARGET ps_data_source_sim)
  add_executable(test_data_source unit/test_data_source.cpp)
  target_include_directories(test_data_source PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/plugin/include)
  target_link_libraries(test_data_source PRIVATE ps_data_source_sim ps_plugin_host ps_kernel)
  add_test(NAME data_source COMMAND test_data_source)
endif()

if(TARGET ps_plugin_host)
  add_library(ps_demo_plugin MODULE external_plugin/DemoPlugin.cpp)
  target_include_directories(ps_demo_plugin PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/plugin/include)
  add_custom_command(TARGET ps_demo_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/test/external_plugin/manifest.json
            ${PROJECT_SOURCE_DIR}/test/external_plugin/dialog.ui
            $<TARGET_FILE_DIR:ps_demo_plugin>)
  set_target_properties(ps_demo_plugin PROPERTIES
    PREFIX "" OUTPUT_NAME "demo_plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins")
  ps_set_warnings(ps_demo_plugin)

  add_library(ps_bad_plugin MODULE external_plugin/BadPlugin.cpp)
  target_include_directories(ps_bad_plugin PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/plugin/include)
  set_target_properties(ps_bad_plugin PROPERTIES
    PREFIX "" OUTPUT_NAME "bad_plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins")
  ps_set_warnings(ps_bad_plugin)

  add_executable(test_plugin_runtime unit/test_plugin_runtime.cpp)
  target_include_directories(test_plugin_runtime PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/plugin/include)
  target_compile_definitions(test_plugin_runtime PRIVATE
    PS_PLUGIN_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/plugins")
  target_link_libraries(test_plugin_runtime PRIVATE ps_plugin_host)
  ps_set_warnings(test_plugin_runtime)
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

# P2 Rust SDK 样板：cdylib 只依赖宿主函数表，不链接第二份 kernel。
find_program(CARGO_EXECUTABLE NAMES cargo HINTS "$ENV{HOME}/.cargo/bin" /usr/local/bin)
if(CARGO_EXECUTABLE)
  if(WIN32)
    set(RUST_PLUGIN_NAME "ps_data_loader_rustcsv.dll")
  else()
    set(RUST_PLUGIN_NAME "libps_data_loader_rustcsv.so")
  endif()
  set(RUST_PLUGIN_DIR ${CMAKE_BINARY_DIR}/rust-plugin)
  set(RUST_PLUGIN_SO ${RUST_PLUGIN_DIR}/${RUST_PLUGIN_NAME})
  set(RUST_APP_PLUGIN_DIR ${CMAKE_BINARY_DIR}/src/app/plugins/data_loader_rustcsv)
  add_custom_command(OUTPUT ${RUST_PLUGIN_SO}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RUST_PLUGIN_DIR} ${RUST_APP_PLUGIN_DIR}
    COMMAND ${CARGO_EXECUTABLE} build --manifest-path
            ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/rustcsv/Cargo.toml --release
            --target-dir ${CMAKE_BINARY_DIR}/rust-target
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/rust-target/release/${RUST_PLUGIN_NAME} ${RUST_PLUGIN_SO}
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/rust-target/release/${RUST_PLUGIN_NAME}
            ${RUST_APP_PLUGIN_DIR}/${RUST_PLUGIN_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy
            ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/rustcsv/manifest.json
            ${RUST_APP_PLUGIN_DIR}/manifest.json
    DEPENDS ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/rustcsv/src/lib.rs
            ${PROJECT_SOURCE_DIR}/src/plugin/sdk/rust/src/lib.rs
            ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/rustcsv/manifest.json
    COMMENT "Building/installing Rust data_loader sample")
  add_custom_target(ps_data_loader_rustcsv ALL DEPENDS ${RUST_PLUGIN_SO})

  add_executable(test_rust_plugin unit/test_rust_plugin.cpp)
  target_include_directories(test_rust_plugin PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/plugin/include ${PROJECT_SOURCE_DIR}/src/kernel/include)
  target_compile_definitions(test_rust_plugin PRIVATE PS_RUST_PLUGIN_DIR="${RUST_PLUGIN_DIR}")
  target_link_libraries(test_rust_plugin PRIVATE ps_plugin_host ps_kernel)
  ps_set_warnings(test_rust_plugin)
  add_dependencies(test_rust_plugin ps_data_loader_rustcsv)
  add_test(NAME rust_plugin COMMAND test_rust_plugin)
endif()

find_package(Lua 5.4 QUIET)
if(LUA_FOUND)
  add_executable(test_script_engine
    unit/test_script_engine.cpp
    ${PROJECT_SOURCE_DIR}/src/app/script/ScriptEngine.cpp)
  target_include_directories(test_script_engine PRIVATE
    ${PROJECT_SOURCE_DIR}/src/app
    ${LUA_INCLUDE_DIR})
  target_compile_definitions(test_script_engine PRIVATE PS_HAVE_LUA=1)
  target_link_libraries(test_script_engine PRIVATE Qt6::Core ${LUA_LIBRARIES})
  add_test(NAME script_engine COMMAND test_script_engine)
endif()
