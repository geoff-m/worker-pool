
find_package(GoogleTest QUIET)
if(GoogleTest_FOUND)
    message(STATUS "Will use found GoogleTest package")
else()
    message(STATUS "GoogleTest package not found--will attempt to download it")
    include(FetchContent)
    FetchContent_Declare(
            GoogleTest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        52eb8108c5bdec04579160ae17225d66034bd723 # v1.17.0
    )
    FetchContent_MakeAvailable(GoogleTest)
endif()

add_executable(test-worker-pool
        src/BasicTests.cpp
        src/StressTests.cpp
        src/PreventWorkOffPoolThreads.cpp
        src/include/TestUtils.h
        src/TestUtils.cpp
        src/TestThreadFactory.cpp
        src/ResultAccess.cpp
        src/TestWaitAll.cpp
        src/TestName.cpp
        src/TimedWait.cpp
        src/TestDetectThreads.cpp
        src/TestExceptions.cpp
        src/TestCancel.cpp
        src/TestMultipool.cpp
        src/TestDetectDeadlock.cpp
        src/TestGetState.cpp
        src/TestAwaitIdleThread.cpp
        src/TestAwaitIdlePool.cpp
)

target_include_directories(test-worker-pool PRIVATE src/include)

set_target_properties(test-worker-pool PROPERTIES CXX_STANDARD 20)
target_link_libraries(test-worker-pool
        PRIVATE worker-pool
        PRIVATE GTest::gtest_main)

target_compile_options(test-worker-pool PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror -Wall -Wextra -Wpedantic>)

include(GoogleTest)
gtest_discover_tests(test-worker-pool)

