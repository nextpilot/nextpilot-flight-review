# ps_kernel：把 src/kernel 的 Rust 静态库接进 CMake。
# 约定（D16）：src/kernel 是 Cargo workspace 根，ps-abi 产出 staticlib + 唯一对外头文件。
# 有网可用 Corrosion（CPS_USE_CORROSION），离线环境走 cargo 直调，两条路等价。
function(ps_import_kernel target)
  find_program(PS_CARGO NAMES cargo HINTS "$ENV{HOME}/.cargo/bin" "$ENV{USERPROFILE}/.cargo/bin")
  set(PS_CARGO "${PS_CARGO}" CACHE FILEPATH "cargo 可执行文件")
  if(NOT PS_CARGO)
    message(FATAL_ERROR "找不到 cargo：内核是 Rust 写的，请先安装 rustup 或设置 -DPS_CARGO=<path>")
  endif()

  set(kernel_dir "${PROJECT_SOURCE_DIR}/src/kernel")
  set(profile release)

  if(WIN32)
    set(kernel_lib "${kernel_dir}/target/${profile}/ps_abi.lib")
  else()
    set(kernel_lib "${kernel_dir}/target/${profile}/libps_abi.a")
  endif()

  # 用 BYPRODUCTS + 每次都跑的 custom target：cargo 自己做增量判断（几十毫秒），
  # 避免"库源码改了但 CMake 认为 OUTPUT 没变"导致链的是旧 .a。
  add_custom_target(ps_kernel_cargo
    COMMAND "${PS_CARGO}" build --manifest-path "${kernel_dir}/Cargo.toml" -p ps-abi --release
    WORKING_DIRECTORY "${kernel_dir}"
    BYPRODUCTS "${kernel_lib}"
    COMMENT "cargo build -p ps-abi (${profile})"
    VERBATIM)

  if(EXISTS "${kernel_lib}")
    set(kernel_lib_exists TRUE)
  endif()

  add_library(${target} INTERFACE)
  add_dependencies(${target} ps_kernel_cargo)
  target_include_directories(${target} INTERFACE "${kernel_dir}/ps-abi/include")
  target_link_libraries(${target} INTERFACE "${kernel_lib}")
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(${target} INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
  endif()
endfunction()
