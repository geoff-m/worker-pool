#include "gtest/gtest.h"
#include "WorkerPool.h"

TEST(StressTests, ManyTasks) {
    WorkerPool pool(2);
    const size_t TASK_COUNT = 100000;
    std::vector<WorkerPool::Task<size_t>> futures;
    futures.reserve(TASK_COUNT);
    for (size_t i = 1; i <= TASK_COUNT; ++i) {
        futures.emplace_back(pool.add([=] { return i; }));
    }
    size_t sum = 0;
    for (size_t i = 0; i < TASK_COUNT; ++i)
        sum += futures[i].getResult();

    EXPECT_EQ(TASK_COUNT * (TASK_COUNT + 1) / 2, sum);
}

void sleep(int seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

TEST(StressTests, ManyWaits) {
    WorkerPool pool(4);
    const auto startTime = std::chrono::steady_clock::now();
    pool.add([&] {
        auto sub1 = pool.add([&] { sleep(1); });
        auto sub2 = pool.add([&] { sleep(1); });
        auto sub3 = pool.add([&] { sleep(1); });
        auto sub4 = pool.add([&] { sleep(1); });
        sub1.wait();
        sub2.wait();
        sub3.wait();
        sub4.wait();
    }).wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_LT(durationMs, 1500);
}
