cmake_minimum_required(VERSION 3.23)
project(ps_canvas_spike LANGUAGES CXX)

# 前提：JKQTPlotter 已编译安装到一个 prefix（见 README），内核已 cargo build --release
#   cmake -S test/perf/canvas_spike -B build/bench -DCMAKE_PREFIX_PATH=/path/to/jkqt-prefix
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Svg Xml OpenGL)
find_package(JKQTPlotter6 REQUIRED)
find_package(Threads REQUIRED)

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_executable(ps_canvas_spike bench.cpp ${REPO}/src/application/modules/plotting/widget/canvas/PlottingCanvas.cpp)
target_include_directories(ps_canvas_spike PRIVATE ${REPO}/src/application/modules/plotting/widget/canvas ${REPO}/src/application/kernel/include)
target_link_libraries(ps_canvas_spike PRIVATE JKQTPlotter6::JKQTPlotter6 Qt6::Widgets
  ${REPO}/src/application/kernel/target/release/libps_abi.a Threads::Threads ${CMAKE_DL_LIBS})
