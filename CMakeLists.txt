cmake_minimum_required(VERSION 3.20)
project(worker_pool)
set(CMAKE_CXX_STANDARD 20)
include(cmake/asan.cmake)
include(cmake/msan.cmake)
include(cmake/tsan.cmake)
include(cmake/ubsan.cmake)

add_library(worker-pool
        include/WorkerPool.h
        src/WorkerPool.cpp)
target_include_directories(worker-pool PUBLIC include)
option(WORKER_POOL_LOGGING "Enable logging to stdout for worker pools" OFF)
if (WORKER_POOL_LOGGING)
    target_compile_definitions(worker-pool PRIVATE WORKER_POOL_LOGGING)
endif ()

option(WORKER_POOL_TEST "Enable unit tests" ON)
if (WORKER_POOL_TEST)
    enable_testing()
    add_subdirectory(test)
endif ()