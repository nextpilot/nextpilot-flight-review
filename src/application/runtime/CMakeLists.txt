add_subdirectory(host)

add_library(ps_runtime STATIC
  ModuleBridge.cpp
  SettingsStore.cpp
  SessionState.cpp
  LogService.cpp
  ExtensionHost.cpp
)
target_include_directories(ps_runtime PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/host/include
)
target_include_directories(ps_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_runtime PUBLIC Qt6::Core ps_host)
