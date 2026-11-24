include_guard(GLOBAL)

include(FetchContent)
include(GNUInstallDirs)

option(PS_DEPS_FETCH "缺少 zlib/sqlite3/zstd/lz4/nanoarrow 时自动拉源码构建" ON)
option(PS_DEPS_PREFER_SYSTEM "优先使用系统或 vcpkg 已安装的库" ON)
option(PS_DEPS_INSTALL "为源码构建的依赖生成 install 规则（component=ps_deps）" OFF)
set(PS_DEPS_ROOT "" CACHE PATH "已安装 zlib/sqlite3/zstd/lz4 的前缀，含 include 和 lib")
set(PS_DEPS_ZLIB_URL "https://gitee.com/nphelper/zlib/repository/archive/v1.3.1.zip" CACHE STRING "zlib 源码包")
set(PS_DEPS_ZSTD_URL "https://gitee.com/nphelper/zstd/repository/archive/v1.5.7.zip" CACHE STRING "zstd 源码包")
set(PS_DEPS_LZ4_URL "https://gitee.com/nphelper/lz4/repository/archive/v1.10.0.zip" CACHE STRING "lz4 源码包")
# nanoarrow 走 gitee 镜像（nphelper/nanoarrow），避免从 GitHub release 拉包时网络超时。
# 第一个 URL 是 gitee 镜像的 tag 归档，与 zlib/zstd/lz4 的写法一致；
# 第二个 URL 是上游 GitHub release 包，作为镜像未同步时的兜底。
# 镜像仓库：https://gitee.com/nphelper/nanoarrow  tag：apache-arrow-nanoarrow-0.9.0
set(PS_DEPS_NANOARROW_URL
    "https://gitee.com/nphelper/nanoarrow/repository/archive/apache-arrow-nanoarrow-0.9.0.zip"
    "https://github.com/apache/arrow-nanoarrow/releases/download/apache-arrow-nanoarrow-0.9.0/apache-arrow-nanoarrow-0.9.0.tar.gz"
    CACHE STRING "nanoarrow 源码包")
# 源码包校验哈希。留空表示不校验（与 zlib/zstd/lz4 一致）。
# 注意：多个候选 URL 只能共用一个哈希值，而 gitee 归档 zip 是重新打包的、
# 与上游 release tar.gz 哈希不同，因此同步镜像后需填入 gitee zip 的实际哈希，
# 或直接留空。填错会导致下载成功但哈希校验失败，构建中断。
set(PS_DEPS_NANOARROW_SHA256 "" CACHE STRING "nanoarrow 源码包 SHA256（留空不校验）")

if(PS_DEPS_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${PS_DEPS_ROOT}")
endif()
if(DEFINED ENV{VCPKG_ROOT})
    set(_ps_vcpkg_triplet "$ENV{VCPKG_DEFAULT_TRIPLET}")
    if(NOT _ps_vcpkg_triplet)
        if(WIN32)
            set(_ps_vcpkg_triplet "x64-windows")
        else()
            set(_ps_vcpkg_triplet "x64-linux")
        endif()
    endif()
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/${_ps_vcpkg_triplet}")
endif()

function(ps_dep_missing name packages)
    if(NOT PS_DEPS_FETCH)
        message(FATAL_ERROR
            "未找到 ${name}。请安装后重新配置，或保持 PS_DEPS_FETCH=ON 自动拉源码。\n"
            "vcpkg 方式：vcpkg install ${packages} --triplet x64-windows\n"
            "然后把 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake 传给 CMake。")
    endif()
endfunction()

function(ps_make_interface_target name)
    if(NOT TARGET ${name})
        add_library(${name} INTERFACE)
    endif()
endfunction()

function(ps_resolve_zlib)
    if(PS_DEPS_ROOT)
        find_path(PS_ZLIB_INCLUDE_DIR NAMES zlib.h PATHS "${PS_DEPS_ROOT}/include" NO_DEFAULT_PATH)
        find_library(PS_ZLIB_LIBRARY NAMES zd z zlib zlibstatic zdll
            PATHS "${PS_DEPS_ROOT}/lib" NO_DEFAULT_PATH)
        if(PS_ZLIB_INCLUDE_DIR AND PS_ZLIB_LIBRARY)
            ps_make_interface_target(ps_zlib)
            target_include_directories(ps_zlib INTERFACE ${PS_ZLIB_INCLUDE_DIR})
            target_link_libraries(ps_zlib INTERFACE ${PS_ZLIB_LIBRARY})
            message(STATUS "PlotScope deps: zlib = ${PS_DEPS_ROOT}")
            return()
        endif()
    endif()

    if(PS_DEPS_PREFER_SYSTEM)
        find_package(ZLIB QUIET)
        if(ZLIB_FOUND)
            ps_make_interface_target(ps_zlib)
            target_link_libraries(ps_zlib INTERFACE ZLIB::ZLIB)
            message(STATUS "PlotScope deps: zlib = system")
            return()
        endif()

        find_path(PS_ZLIB_INCLUDE_DIR NAMES zlib.h)
        find_library(PS_ZLIB_LIBRARY NAMES z zlib zlibstatic zdll)
        if(PS_ZLIB_INCLUDE_DIR AND PS_ZLIB_LIBRARY)
            ps_make_interface_target(ps_zlib)
            target_include_directories(ps_zlib INTERFACE ${PS_ZLIB_INCLUDE_DIR})
            target_link_libraries(ps_zlib INTERFACE ${PS_ZLIB_LIBRARY})
            message(STATUS "PlotScope deps: zlib = ${PS_ZLIB_LIBRARY}")
            return()
        endif()
    endif()

    ps_dep_missing("zlib" "zlib")
    message(STATUS "PlotScope deps: zlib = FetchContent Gitee")
    FetchContent_Declare(zlib
        URL ${PS_DEPS_ZLIB_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)

    ps_make_interface_target(ps_zlib)
    if(TARGET zlibstatic)
        set(_ps_zlib_target zlibstatic)
    elseif(TARGET zlib)
        set(_ps_zlib_target zlib)
    else()
        message(FATAL_ERROR "zlib FetchContent 完成，但没有生成 zlibstatic/zlib 目标")
    endif()
    target_link_libraries(ps_zlib INTERFACE ${_ps_zlib_target})

    if(PS_DEPS_INSTALL)
        install(FILES "$<TARGET_FILE:${_ps_zlib_target}>"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT ps_deps)
        install(FILES ${zlib_SOURCE_DIR}/zlib.h ${zlib_BINARY_DIR}/zconf.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT ps_deps)
    endif()
endfunction()

function(ps_resolve_zstd)
    if(PS_DEPS_ROOT)
        find_path(PS_ZSTD_INCLUDE_DIR NAMES zstd.h PATHS "${PS_DEPS_ROOT}/include" NO_DEFAULT_PATH)
        find_library(PS_ZSTD_LIBRARY NAMES zstdd zstd libzstd zstd_static
            PATHS "${PS_DEPS_ROOT}/lib" NO_DEFAULT_PATH)
        if(PS_ZSTD_INCLUDE_DIR AND PS_ZSTD_LIBRARY)
            ps_make_interface_target(ps_zstd)
            target_include_directories(ps_zstd INTERFACE ${PS_ZSTD_INCLUDE_DIR})
            target_link_libraries(ps_zstd INTERFACE ${PS_ZSTD_LIBRARY})
            message(STATUS "PlotScope deps: zstd = ${PS_DEPS_ROOT}")
            return()
        endif()
    endif()

    if(PS_DEPS_PREFER_SYSTEM)
        find_path(PS_ZSTD_INCLUDE_DIR NAMES zstd.h)
        find_library(PS_ZSTD_LIBRARY NAMES zstd libzstd zstd_static)
        if(PS_ZSTD_INCLUDE_DIR AND PS_ZSTD_LIBRARY)
            ps_make_interface_target(ps_zstd)
            target_include_directories(ps_zstd INTERFACE ${PS_ZSTD_INCLUDE_DIR})
            target_link_libraries(ps_zstd INTERFACE ${PS_ZSTD_LIBRARY})
            message(STATUS "PlotScope deps: zstd = ${PS_ZSTD_LIBRARY}")
            return()
        endif()
    endif()

    ps_dep_missing("zstd" "zstd")
    message(STATUS "PlotScope deps: zstd = FetchContent Gitee")
    FetchContent_Declare(zstd
        URL ${PS_DEPS_ZSTD_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(zstd)
    if(NOT zstd_POPULATED)
        FetchContent_Populate(zstd)
    endif()

    if(EXISTS ${zstd_SOURCE_DIR}/build/cmake/CMakeLists.txt)
        set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
        add_subdirectory(${zstd_SOURCE_DIR}/build/cmake ${zstd_BINARY_DIR})
        ps_make_interface_target(ps_zstd)
        if(TARGET libzstd_static)
            set(_ps_zstd_target libzstd_static)
        elseif(TARGET zstd_static)
            set(_ps_zstd_target zstd_static)
        else()
            message(FATAL_ERROR "zstd CMake 完成，但没有生成 libzstd_static/zstd_static 目标")
        endif()
        set(_ps_zstd_include ${zstd_SOURCE_DIR}/lib)
    elseif(EXISTS ${zstd_SOURCE_DIR}/zstd.h)
        # 部分国内镜像把 zstd C 源码平铺在根目录，直接编译静态库。
        file(GLOB ZSTD_C_SOURCES CONFIGURE_DEPENDS ${zstd_SOURCE_DIR}/*.c)
        list(FILTER ZSTD_C_SOURCES EXCLUDE REGEX ".*/(cover|fastcover|zdict|divsufsort)\\.c$")
        add_library(ps_zstd STATIC ${ZSTD_C_SOURCES})
        target_include_directories(ps_zstd PUBLIC ${zstd_SOURCE_DIR})
        target_compile_definitions(ps_zstd PRIVATE ZSTD_DISABLE_ASM)
        set_target_properties(ps_zstd PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            OUTPUT_NAME zstd)
        set(_ps_zstd_target ps_zstd)
        set(_ps_zstd_include ${zstd_SOURCE_DIR})
    else()
        message(FATAL_ERROR "zstd 源码中没有 build/cmake，也没有根目录 zstd.h")
    endif()

    target_link_libraries(ps_zstd INTERFACE ${_ps_zstd_target})
    if(PS_DEPS_INSTALL)
        install(FILES "$<TARGET_FILE:${_ps_zstd_target}>"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT ps_deps)
        install(FILES
            ${_ps_zstd_include}/zstd.h
            ${_ps_zstd_include}/zstd_errors.h
            ${_ps_zstd_include}/zdict.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT ps_deps)
    endif()
endfunction()

function(ps_resolve_lz4)
    if(PS_DEPS_ROOT)
        find_path(PS_LZ4_INCLUDE_DIR NAMES lz4.h PATHS "${PS_DEPS_ROOT}/include" NO_DEFAULT_PATH)
        find_library(PS_LZ4_LIBRARY NAMES lz4d lz4 lz4_static
            PATHS "${PS_DEPS_ROOT}/lib" NO_DEFAULT_PATH)
        if(PS_LZ4_INCLUDE_DIR AND PS_LZ4_LIBRARY)
            ps_make_interface_target(ps_lz4)
            target_include_directories(ps_lz4 INTERFACE ${PS_LZ4_INCLUDE_DIR})
            target_link_libraries(ps_lz4 INTERFACE ${PS_LZ4_LIBRARY})
            message(STATUS "PlotScope deps: lz4 = ${PS_DEPS_ROOT}")
            return()
        endif()
    endif()

    if(PS_DEPS_PREFER_SYSTEM)
        find_path(PS_LZ4_INCLUDE_DIR NAMES lz4.h)
        find_library(PS_LZ4_LIBRARY NAMES lz4 lz4_static)
        if(PS_LZ4_INCLUDE_DIR AND PS_LZ4_LIBRARY)
            ps_make_interface_target(ps_lz4)
            target_include_directories(ps_lz4 INTERFACE ${PS_LZ4_INCLUDE_DIR})
            target_link_libraries(ps_lz4 INTERFACE ${PS_LZ4_LIBRARY})
            message(STATUS "PlotScope deps: lz4 = ${PS_LZ4_LIBRARY}")
            return()
        endif()
    endif()

    ps_dep_missing("lz4" "lz4")
    message(STATUS "PlotScope deps: lz4 = FetchContent Gitee")
    FetchContent_Declare(lz4
        URL ${PS_DEPS_LZ4_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(lz4)
    if(NOT lz4_POPULATED)
        FetchContent_Populate(lz4)
    endif()

    if(EXISTS ${lz4_SOURCE_DIR}/build/cmake/CMakeLists.txt)
        set(LZ4_BUILD_CLI OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
        add_subdirectory(${lz4_SOURCE_DIR}/build/cmake ${lz4_BINARY_DIR})
        ps_make_interface_target(ps_lz4)
        if(TARGET lz4_static)
            set(_ps_lz4_target lz4_static)
        elseif(TARGET lz4)
            set(_ps_lz4_target lz4)
        else()
            message(FATAL_ERROR "lz4 CMake 完成，但没有生成 lz4_static/lz4 目标")
        endif()
        set(_ps_lz4_include ${lz4_SOURCE_DIR}/lib)
    elseif(EXISTS ${lz4_SOURCE_DIR}/lib/lz4.c)
        # OpenHarmony 镜像不带 CMake，只编译块解压需要的 lz4.c。
        add_library(ps_lz4 STATIC ${lz4_SOURCE_DIR}/lib/lz4.c)
        target_include_directories(ps_lz4 PUBLIC ${lz4_SOURCE_DIR}/lib)
        set_target_properties(ps_lz4 PROPERTIES POSITION_INDEPENDENT_CODE ON)
        set(_ps_lz4_target ps_lz4)
        set(_ps_lz4_include ${lz4_SOURCE_DIR}/lib)
    else()
        message(FATAL_ERROR "lz4 源码中没有 build/cmake，也没有 lib/lz4.c")
    endif()

    target_link_libraries(ps_lz4 INTERFACE ${_ps_lz4_target})
    if(PS_DEPS_INSTALL)
        install(FILES "$<TARGET_FILE:${_ps_lz4_target}>"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT ps_deps)
        install(FILES
            ${_ps_lz4_include}/lz4.h
            ${_ps_lz4_include}/lz4hc.h
            ${_ps_lz4_include}/lz4frame.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT ps_deps)
    endif()
endfunction()

# === sqlite3：rosbag2 读 .db3 需要；系统/vcpkg 优先，缺失时拉官方 amalgamation 编静态库 ===
set(PS_DEPS_SQLITE3_URL "https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip"
    CACHE STRING "sqlite3 amalgamation 源码包")
function(ps_resolve_sqlite3)
    if(PS_DEPS_ROOT)
        find_path(PS_SQLITE3_INCLUDE_DIR NAMES sqlite3.h PATHS "${PS_DEPS_ROOT}/include" NO_DEFAULT_PATH)
        find_library(PS_SQLITE3_LIBRARY NAMES sqlite3 sqlite3_static sqlite
            PATHS "${PS_DEPS_ROOT}/lib" NO_DEFAULT_PATH)
        if(PS_SQLITE3_INCLUDE_DIR AND PS_SQLITE3_LIBRARY)
            ps_make_interface_target(ps_sqlite3)
            target_include_directories(ps_sqlite3 INTERFACE ${PS_SQLITE3_INCLUDE_DIR})
            target_link_libraries(ps_sqlite3 INTERFACE ${PS_SQLITE3_LIBRARY})
            message(STATUS "PlotScope deps: sqlite3 = ${PS_DEPS_ROOT}")
            return()
        endif()
    endif()

    if(PS_DEPS_PREFER_SYSTEM)
        find_package(SQLite3 QUIET)
        if(SQLite3_FOUND)
            ps_make_interface_target(ps_sqlite3)
            target_link_libraries(ps_sqlite3 INTERFACE SQLite::SQLite3)
            message(STATUS "PlotScope deps: sqlite3 = system")
            return()
        endif()
    endif()

    ps_dep_missing("sqlite3" "sqlite3")
    message(STATUS "PlotScope deps: sqlite3 = FetchContent amalgamation")
    FetchContent_Declare(sqlite3
        URL ${PS_DEPS_SQLITE3_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(sqlite3)
    if(NOT sqlite3_POPULATED)
        FetchContent_Populate(sqlite3)
    endif()

    if(NOT EXISTS ${sqlite3_SOURCE_DIR}/sqlite3.c)
        message(FATAL_ERROR "sqlite3 源码包里没有 sqlite3.c：${sqlite3_SOURCE_DIR}")
    endif()
    add_library(ps_sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(ps_sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
    # 默认单线程库够用；rosbag2 只在解析线程内串行读库。
    target_compile_definitions(ps_sqlite3 PUBLIC SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION=1)
    set_target_properties(ps_sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON OUTPUT_NAME sqlite3)

    # 与 zlib 一致：由本工程编出的静态库才能保证 MSVC 运行库与顶层目标同为 /MD。
    if(PS_DEPS_INSTALL)
        install(FILES "$<TARGET_FILE:ps_sqlite3>"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT ps_deps)
        install(FILES ${sqlite3_SOURCE_DIR}/sqlite3.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT ps_deps)
    endif()
endfunction()

function(ps_resolve_nanoarrow)
    if(PS_DEPS_PREFER_SYSTEM)
        find_package(nanoarrow 0.9 CONFIG QUIET)
        if(nanoarrow_FOUND)
            ps_make_interface_target(ps_nanoarrow)
            target_link_libraries(ps_nanoarrow INTERFACE nanoarrow::nanoarrow)
            if(TARGET nanoarrow::nanoarrow_ipc)
                target_link_libraries(ps_nanoarrow INTERFACE nanoarrow::nanoarrow_ipc)
            endif()
            message(STATUS "PlotScope deps: nanoarrow = system")
            return()
        endif()
    endif()

    ps_dep_missing("nanoarrow" "nanoarrow")
    message(STATUS "PlotScope deps: nanoarrow = FetchContent 0.9.0 (含 IPC)")
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(NANOARROW_IPC ON CACHE BOOL "" FORCE)
    set(NANOARROW_IPC_WITH_ZSTD OFF CACHE BOOL "" FORCE)
    set(NANOARROW_IPC_WITH_LZ4 OFF CACHE BOOL "" FORCE)
    # 多个候选 URL 共用一段 FetchContent 声明：CMake 依次尝试，第一个成功的即被采用。
    # 哈希校验只在明确给出时才启用——gitee 归档 zip 与上游 release tar.gz 哈希不同，
    # 混用会导致下载成功但校验失败。
    set(_ps_nanoarrow_fetch_args
        URL ${PS_DEPS_NANOARROW_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    if(PS_DEPS_NANOARROW_SHA256)
        list(APPEND _ps_nanoarrow_fetch_args URL_HASH SHA256=${PS_DEPS_NANOARROW_SHA256})
    endif()
    FetchContent_Declare(nanoarrow ${_ps_nanoarrow_fetch_args})
    FetchContent_MakeAvailable(nanoarrow)
    # 上游同时定义 shared 与测试目标；只保留静态库被本工程链接，其余排除在 all 之外。
    if(nanoarrow_SOURCE_DIR)
        set_property(DIRECTORY "${nanoarrow_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL ON)
    endif()
    ps_make_interface_target(ps_nanoarrow)
    target_link_libraries(ps_nanoarrow INTERFACE nanoarrow_static)
    if(TARGET nanoarrow_ipc_static)
        target_link_libraries(ps_nanoarrow INTERFACE nanoarrow_ipc_static)
    endif()
endfunction()

# 只有仓库当前真正使用的依赖才在这里解析。
# zstd / lz4 保留函数体不调用：MDF 只用到 zlib，rosbag2 用 sqlite3；
# 将来某个格式真的用上 zstd/lz4，再在这里加一行调用（不要在没人用时白白拉源码）。
ps_resolve_zlib()
ps_resolve_sqlite3()
ps_resolve_nanoarrow()
