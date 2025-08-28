cmake_minimum_required(VERSION 4.0)
project(worker_pool)
set(CMAKE_CXX_STANDARD 20)
include(cmake/asan.cmake)
include(cmake/msan.cmake)
include(cmake/tsan.cmake)
include(cmake/ubsan.cmake)

add_library(worker-pool INTERFACE)
target_include_directories(worker-pool INTERFACE include)

add_subdirectory(thirdparty/googletest)
enable_testing()
add_subdirectory(test)
