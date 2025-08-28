#include "gtest/gtest.h"
#include "WorkerPool.h"

TEST(StressTests, ManyTasks) {
    WorkerPool pool(2);
    const size_t TASK_COUNT = 100000;
    std::vector<std::future<size_t>> futures;
    futures.reserve(TASK_COUNT);
    for (size_t i = 1; i <= TASK_COUNT; ++i) {
        futures.emplace_back(pool.add([=] { return i; }));
    }
    size_t sum = 0;
    for (size_t i = 0; i < TASK_COUNT; ++i)
        sum += futures[i].get();

    EXPECT_EQ(TASK_COUNT * (TASK_COUNT + 1) / 2, sum);
}
