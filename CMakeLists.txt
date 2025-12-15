cmake_minimum_required(VERSION 3.20)
project(worker_pool)
set(CMAKE_CXX_STANDARD 20)
include(cmake/asan.cmake)
include(cmake/msan.cmake)
include(cmake/tsan.cmake)
include(cmake/ubsan.cmake)

add_library(worker-pool
        include/worker-pool/worker-pool.h
        src/worker-pool.cpp)
target_include_directories(worker-pool PUBLIC include)
option(WORKER_POOL_LOGGING "Enable logging to stdout for worker pools" OFF)
if (WORKER_POOL_LOGGING)
    target_compile_definitions(worker-pool PRIVATE WORKER_POOL_LOGGING)
endif ()

option(WORKER_POOL_DEADLOCK_DETECTION "Whether to detect potential deadlocks" OFF)

if (NOT DEFINED WORKER_POOL_DEADLOCK_DETECTION_ACTION)
    set(WORKER_POOL_DEADLOCK_DETECTION_ACTION "Throw")
endif ()

string(TOLOWER ${WORKER_POOL_DEADLOCK_DETECTION_ACTION} WORKER_POOL_DEADLOCK_DETECTION_ACTION)

if (WORKER_POOL_DEADLOCK_DETECTION)
    set(WORKER_POOL_DEADLOCK_DETECTION_ACTION_VALUES throw terminate)
    list(FIND WORKER_POOL_DEADLOCK_DETECTION_ACTION_VALUES ${WORKER_POOL_DEADLOCK_DETECTION_ACTION} index)
    if (index EQUAL -1)
        message(FATAL_ERROR "WORKER_POOL_DEADLOCK_DETECTION_ACTION value of ${WORKER_POOL_DEADLOCK_DETECTION_ACTION} must be one of: ${WORKER_POOL_DEADLOCK_DETECTION_ACTION_VALUES}")
    endif ()
    target_compile_definitions(worker-pool PUBLIC
            WORKER_POOL_DEADLOCK_DETECTION)
    if (${WORKER_POOL_DEADLOCK_DETECTION_ACTION} STREQUAL "terminate")
        target_compile_definitions(worker-pool PUBLIC
                WORKER_POOL_DEADLOCK_DETECTION_TERMINATE
                WORKER_POOL_DEADLOCK_DETECTION_FATAL)
    endif ()
endif ()


option(WORKER_POOL_TEST "Enable unit tests" OFF)
if (WORKER_POOL_TEST)
    enable_testing()
    add_subdirectory(test)
endif ()