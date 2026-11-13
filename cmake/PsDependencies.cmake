include_guard(GLOBAL)

include(FetchContent)
include(GNUInstallDirs)

option(PS_DEPS_FETCH "缺少 zlib/zstd/lz4 时自动拉源码构建" ON)
option(PS_DEPS_PREFER_SYSTEM "优先使用系统或 vcpkg 已安装的库" ON)
option(PS_DEPS_INSTALL "为源码构建的依赖生成 install 规则（component=ps_deps）" OFF)
set(PS_DEPS_ROOT "" CACHE PATH "已安装 zlib/zstd/lz4 的前缀，含 include 和 lib")
set(PS_DEPS_ZLIB_URL "https://gitee.com/nphelper/zlib/repository/archive/v1.3.1.zip" CACHE STRING "zlib 源码包")
set(PS_DEPS_ZSTD_URL "https://gitee.com/nphelper/zstd/repository/archive/v1.5.7.zip" CACHE STRING "zstd 源码包")
set(PS_DEPS_LZ4_URL "https://gitee.com/nphelper/lz4/repository/archive/v1.10.0.zip" CACHE STRING "lz4 源码包")

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

ps_resolve_zlib()
ps_resolve_zstd()
ps_resolve_lz4()
