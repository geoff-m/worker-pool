
add_executable(test-worker-pool
        src/BasicTests.cpp
        src/StressTests.cpp
)

set_target_properties(test-worker-pool PROPERTIES CXX_STANDARD 20)

target_link_libraries(test-worker-pool
        PRIVATE worker-pool
        PRIVATE GTest::gtest_main)

target_compile_options(test-worker-pool PRIVATE -Werror -Wall -Wextra -Wpedantic -g)
target_link_options(test-worker-pool PRIVATE -g)

include(GoogleTest)
gtest_discover_tests(test-worker-pool)

