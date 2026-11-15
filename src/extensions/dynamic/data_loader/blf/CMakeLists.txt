add_library(ps_data_loader_blf STATIC
  BlfDataLoader.cpp
  BlfParser.cpp
)

target_include_directories(ps_data_loader_blf PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base/modules/runtime/include
)

target_link_libraries(ps_data_loader_blf PRIVATE ps_loader_common ps_zlib)
