#include "TestUtils.h"
#include "WorkerPool.h"
#include <vector>
#include <list>
#include <atomic>
#include <chrono>

using namespace WorkerPool;

TEST(WaitAll, Smart) {
    Pool pool(2, 0, false);
    const auto startTime = std::chrono::steady_clock::now();
    pool.add([&] {
        std::vector<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([] { sleepMs(1000); }));

        // Give time for pool to start sub1.
        sleepMs(100);

        auto sub2 = subtasks.emplace_back(pool.add([] { sleepMs(1000); }));

        // This should do sub2 first instead of naively blocking on sub1.
        pool.waitAll(subtasks);
    }).wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_LT(durationMs, 1500);
}

TEST(WaitAll, Iterators) {
    Pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        std::vector<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool.waitAll(subtasks.begin(), subtasks.end());
    }).wait();
    ASSERT_EQ(2, done);
}

TEST(BasicTests, Vector) {
    Pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        std::vector<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool.waitAll(subtasks);
    }).wait();
    ASSERT_EQ(2, done);
}

TEST(BasicTests, Array) {
    Pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        std::vector<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool.waitAll(subtasks.data(), subtasks.size());
    }).wait();
    ASSERT_EQ(2, done);
}

TEST(BasicTests, Iterable) {
    Pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        // Use std::list because it doesn't have operator[].
        std::list<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool.waitAll(subtasks);
    }).wait();
    ASSERT_EQ(2, done);
}
