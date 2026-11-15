add_executable(test_abi unit/test_abi.cpp)
target_link_libraries(test_abi PRIVATE ps_kernel)
add_test(NAME abi_selftest COMMAND test_abi)

if(EXISTS ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp/PsDataSourceBase.hpp)
  add_executable(test_data_source_sdk unit/test_data_source_sdk.cpp)
  set_target_properties(test_data_source_sdk PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_include_directories(test_data_source_sdk PRIVATE
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
    ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/cpp)
  add_test(NAME data_source_sdk COMMAND test_data_source_sdk)
endif()

if(TARGET ps_data_loader_csv)
  add_executable(test_extension_csv unit/test_extension_csv.cpp)
  target_include_directories(test_extension_csv PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include)
  target_link_libraries(test_extension_csv PRIVATE ps_data_loader_csv ps_kernel ps_host)
  add_test(NAME extension_csv COMMAND test_extension_csv)
endif()

if(TARGET ps_cache)
  add_executable(test_dataset_cache unit/test_dataset_cache.cpp)
  target_include_directories(test_dataset_cache PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include)
  target_link_libraries(test_dataset_cache PRIVATE ps_cache ps_kernel)
  add_test(NAME dataset_cache COMMAND test_dataset_cache)
endif()

if(TARGET ps_data_loader_ulog AND TARGET ps_data_loader_apbin AND TARGET ps_data_loader_asc AND TARGET ps_data_loader_blf)
  add_executable(test_extension_file_formats unit/test_extension_file_formats.cpp)
  target_include_directories(test_extension_file_formats PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include)
  target_compile_definitions(test_extension_file_formats PRIVATE
    PS_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixture")
  target_link_libraries(test_extension_file_formats PRIVATE ps_host
    ps_data_loader_ulog ps_data_loader_apbin ps_data_loader_asc ps_data_loader_blf ps_data_loader_json ps_data_loader_binary ps_data_loader_mdf4 ps_data_loader_rosbag2 ps_kernel)
  if(TARGET ps_gnss)
    target_link_libraries(test_extension_file_formats PRIVATE ps_gnss)
  endif()
  if(TARGET ps_custom_protocol)
    target_link_libraries(test_extension_file_formats PRIVATE ps_custom_protocol)
    target_compile_definitions(test_extension_file_formats PRIVATE PS_HAVE_CUSTOM_PROTOCOL=1)
  endif()
  add_test(NAME extension_file_formats COMMAND test_extension_file_formats)
endif()

if(TARGET ps_live)
  add_executable(test_raw_capture unit/test_raw_capture.cpp)
  target_include_directories(test_raw_capture PRIVATE
    ${PROJECT_SOURCE_DIR}/src/application/modules/datasource/services/live)
  target_link_libraries(test_raw_capture PRIVATE ps_live)
  add_test(NAME raw_capture COMMAND test_raw_capture)
endif()

if(TARGET ps_data_load_mcap)
  add_executable(test_mcap_loader unit/test_mcap_loader.cpp)
  target_include_directories(test_mcap_loader PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include)
  target_link_libraries(test_mcap_loader PRIVATE ps_data_load_mcap ps_host ps_kernel)
  add_test(NAME mcap_loader COMMAND test_mcap_loader)
endif()

if(TARGET ps_live AND TARGET ps_data_load_mcap)
  add_executable(test_stream_recorder unit/test_stream_recorder.cpp)
  target_include_directories(test_stream_recorder PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
    ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/common
    ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/mcap)
  target_link_libraries(test_stream_recorder PRIVATE ps_live ps_data_load_mcap)
  add_test(NAME stream_recorder COMMAND test_stream_recorder)
endif()

if(TARGET ps_data_source_mavlink)
  find_package(Qt6 REQUIRED COMPONENTS Network)
  add_executable(test_mavlink_source unit/test_mavlink_source.cpp)
  target_include_directories(test_mavlink_source PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include)
  target_link_libraries(test_mavlink_source PRIVATE ps_data_source_mavlink ps_host ps_kernel Qt6::Network)
  add_test(NAME mavlink_source COMMAND test_mavlink_source)
endif()

if(TARGET ps_gnss)
  find_package(Qt6 REQUIRED COMPONENTS Network)
  add_executable(test_oem7_source unit/test_oem7_source.cpp)
  target_include_directories(test_oem7_source PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include)
  target_link_libraries(test_oem7_source PRIVATE ps_gnss ps_host ps_kernel Qt6::Network)
  target_compile_definitions(test_oem7_source PRIVATE
    TEST_OEM7_DB="${CMAKE_SOURCE_DIR}/src/extensions/builtin/gnss/oem7_database.json")
  add_test(NAME oem7_source COMMAND test_oem7_source)
endif()

if(TARGET ps_data_source_sim)
  add_executable(test_data_source unit/test_data_source.cpp)
  target_include_directories(test_data_source PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include)
  target_link_libraries(test_data_source PRIVATE ps_data_source_sim ps_host ps_kernel)
  add_test(NAME data_source COMMAND test_data_source)
endif()

if(TARGET ps_protocol)
  add_executable(test_protocol_document unit/test_protocol_document.cpp)
  target_include_directories(test_protocol_document PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(test_protocol_document PRIVATE
    PS_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixture")
  target_link_libraries(test_protocol_document PRIVATE ps_protocol Qt6::Core)
  add_test(NAME protocol_document COMMAND test_protocol_document)
endif()

if(TARGET ps_host)
  add_library(ps_demo_extension MODULE external_extension/DemoExtension.cpp)
  target_include_directories(ps_demo_extension PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include)
  add_custom_command(TARGET ps_demo_extension POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/tests/external_extension/manifest.json
            ${PROJECT_SOURCE_DIR}/tests/external_extension/dialog.ui
            $<TARGET_FILE_DIR:ps_demo_extension>)
  set_target_properties(ps_demo_extension PROPERTIES
    PREFIX "" OUTPUT_NAME "demo_extension"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/extensions")
  ps_set_warnings(ps_demo_extension)

  add_library(ps_bad_extension MODULE external_extension/BadExtension.cpp)
  target_include_directories(ps_bad_extension PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include)
  set_target_properties(ps_bad_extension PROPERTIES
    PREFIX "" OUTPUT_NAME "bad_extension"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/extensions")
  ps_set_warnings(ps_bad_extension)

  add_executable(test_extension_runtime unit/test_extension_runtime.cpp)
  target_include_directories(test_extension_runtime PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include)
  target_compile_definitions(test_extension_runtime PRIVATE
    PS_EXTENSION_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/extensions")
  target_link_libraries(test_extension_runtime PRIVATE ps_host)
  ps_set_warnings(test_extension_runtime)
  add_dependencies(test_extension_runtime ps_demo_extension ps_bad_extension)
  add_test(NAME extension_runtime COMMAND test_extension_runtime)
  file(COPY ${PROJECT_SOURCE_DIR}/tests/external_extension/manifest.json
       ${PROJECT_SOURCE_DIR}/tests/external_extension/dialog.ui
       DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/extensions)
endif()

if(PS_CARGO)
  add_test(NAME kernel_cargo_test
           COMMAND "${PS_CARGO}" test --manifest-path "${PROJECT_SOURCE_DIR}/src/application/kernel/Cargo.toml"
           WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/src/application/kernel")
endif()

# P2 Rust SDK 样板：cdylib 只依赖宿主函数表，不链接第二份 kernel。
find_program(CARGO_EXECUTABLE NAMES cargo HINTS "$ENV{HOME}/.cargo/bin" /usr/local/bin)
if(CARGO_EXECUTABLE)
  if(WIN32)
    set(RUST_EXTENSION_NAME "ps_data_loader_rustcsv.dll")
  else()
    set(RUST_EXTENSION_NAME "libps_data_loader_rustcsv.so")
  endif()
  set(RUST_EXTENSION_DIR ${CMAKE_BINARY_DIR}/rust-extension)
  set(RUST_EXTENSION_SO ${RUST_EXTENSION_DIR}/${RUST_EXTENSION_NAME})
  set(RUST_APP_EXTENSION_DIR ${PS_EXTENSION_OUTPUT_DIR}/data_loader_rustcsv)
  add_custom_command(OUTPUT ${RUST_EXTENSION_SO}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RUST_EXTENSION_DIR} ${RUST_APP_EXTENSION_DIR}
    COMMAND ${CARGO_EXECUTABLE} build --manifest-path
            ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/rustcsv/Cargo.toml --release
            --target-dir ${CMAKE_BINARY_DIR}/rust-target
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/rust-target/release/${RUST_EXTENSION_NAME} ${RUST_EXTENSION_SO}
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/rust-target/release/${RUST_EXTENSION_NAME}
            ${RUST_APP_EXTENSION_DIR}/${RUST_EXTENSION_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy
            ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/rustcsv/manifest.json
            ${RUST_APP_EXTENSION_DIR}/manifest.json
    DEPENDS ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/rustcsv/src/lib.rs
            ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/sdk/rust/src/lib.rs
            ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/data_loader/rustcsv/manifest.json
    COMMENT "Building/installing Rust data_loader sample")
  add_custom_target(ps_data_loader_rustcsv ALL DEPENDS ${RUST_EXTENSION_SO})

  add_executable(test_rust_extension unit/test_rust_extension.cpp)
  target_include_directories(test_rust_extension PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include ${PROJECT_SOURCE_DIR}/src/application/kernel/include)
  target_compile_definitions(test_rust_extension PRIVATE PS_RUST_EXTENSION_DIR="${RUST_EXTENSION_DIR}")
  target_link_libraries(test_rust_extension PRIVATE ps_host ps_kernel)
  ps_set_warnings(test_rust_extension)
  add_dependencies(test_rust_extension ps_data_loader_rustcsv)
  add_test(NAME rust_extension COMMAND test_rust_extension)
endif()

find_package(Lua 5.4 QUIET)
if(LUA_FOUND)
  add_executable(test_script_engine
    unit/test_script_engine.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/builtin/script/ScriptEngine.cpp)
  target_include_directories(test_script_engine PRIVATE
    ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
    ${PROJECT_SOURCE_DIR}/src/extensions/builtin/script
    ${LUA_INCLUDE_DIR})
  target_compile_definitions(test_script_engine PRIVATE PS_HAVE_LUA=1)
  target_link_libraries(test_script_engine PRIVATE Qt6::Core ${LUA_LIBRARIES})
  add_test(NAME script_engine COMMAND test_script_engine)
endif()

find_package(Qt6 QUIET COMPONENTS Widgets)
if(TARGET Qt6::Widgets)
  add_executable(test_video_sync unit/test_video_sync.cpp ${PROJECT_SOURCE_DIR}/src/extensions/builtin/video/VideoSyncWidget.cpp)
  set_target_properties(test_video_sync PROPERTIES AUTOMOC ON)
  target_include_directories(test_video_sync PRIVATE ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
    ${PROJECT_SOURCE_DIR}/src/extensions/builtin/video)
  target_link_libraries(test_video_sync PRIVATE Qt6::Widgets)
  add_test(NAME video_sync COMMAND test_video_sync)
  set_tests_properties(video_sync PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_executable(test_command_registry unit/test_command_registry.cpp ${PROJECT_SOURCE_DIR}/src/application/shell/CommandRegistry.cpp)
target_include_directories(test_command_registry PRIVATE ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/application/shell)
target_link_libraries(test_command_registry PRIVATE Qt6::Core)
add_test(NAME command_registry COMMAND test_command_registry)
