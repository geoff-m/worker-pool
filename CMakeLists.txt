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

set(WORKER_POOL_DEADLOCK_DETECTION_LEVEL "Disabled" CACHE STRING "Level of sensitivity to potential deadlocks")
set_property(CACHE WORKER_POOL_DEADLOCK_DETECTION_LEVEL PROPERTY STRINGS
        Disabled
        Lenient
        Strict
)

set(WORKER_POOL_DEADLOCK_DETECTION_ACTION "Throw" CACHE STRING "How to react to detected deadlocks")
set_property(CACHE WORKER_POOL_DEADLOCK_DETECTION_ACTION PROPERTY STRINGS
        Throw
        Abort
)

string(TOLOWER ${WORKER_POOL_DEADLOCK_DETECTION_LEVEL} WORKER_POOL_DEADLOCK_DETECTION_LEVEL)
string(TOLOWER ${WORKER_POOL_DEADLOCK_DETECTION_ACTION} WORKER_POOL_DEADLOCK_DETECTION_ACTION)

if (${WORKER_POOL_DEADLOCK_DETECTION_LEVEL}> STREQUAL "lenient")
    target_compile_definitions(worker-pool PUBLIC
            WORKER_POOL_DEADLOCK_DETECTION)
elseif (${WORKER_POOL_DEADLOCK_DETECTION_LEVEL} STREQUAL "strict")
    target_compile_definitions(worker-pool PUBLIC
            WORKER_POOL_DEADLOCK_DETECTION
            WORKER_POOL_DEADLOCK_DETECTION_STRICT)
endif ()
if (${WORKER_POOL_DEADLOCK_DETECTION_ACTION} STREQUAL "abort")
    target_compile_definitions(worker-pool PRIVATE
            WORKER_POOL_DEADLOCK_DETECTION_ABORT)
endif ()

option(WORKER_POOL_TEST "Enable unit tests" OFF)
if (WORKER_POOL_TEST)
    enable_testing()
    add_subdirectory(test)
endif ()