
include(CMakePrintHelpers)

message(STATUS "/*****************************************************************")
message(STATUS " *     _   __             __   ____   _  __        __")
message(STATUS " *    / | / /___   _  __ / /_ / __ \\ (_)/ /____   / /_")
message(STATUS " *   /  |/ // _ \\ | |/_// __// /_/ // // // __ \\ / __/")
message(STATUS " *  / /|  //  __/_>  < / /_ / ____// // // /_/ // /_")
message(STATUS " * /_/ |_/ \\___//_/|_| \\__//_/    /_//_/ \\____/ \\__/")
message(STATUS " *")
message(STATUS " * Copyright All Reserved © 2015-${CURRENT_YEAR} NextPilot Development Team")
message(STATUS " ******************************************************************/")


cmake_minimum_required(VERSION 3.10.2)

project(
  Nextpilot-Flight-Review
  LANGUAGES C CXX
  VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}  "${CMAKE_CURRENT_LIST_DIR}/cmake")


# find python
find_package(Python3)
if(NOT PYTHONINTERP_FOUND)
endif()


include(helper)
message(STATUS "${CMAKE_CURRENT_SOURCE_DIR}")
add_all_subdirectory(${CMAKE_CURRENT_SOURCE_DIR})
