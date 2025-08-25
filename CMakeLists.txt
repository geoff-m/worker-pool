cmake_minimum_required(VERSION 4.0)
project(worker_pool)

set(CMAKE_CXX_STANDARD 20)

add_executable(worker_pool main.cpp
        src/WorkerPool.cpp
        include/WorkerPool.h)

target_include_directories(worker_pool
        PUBLIC include)