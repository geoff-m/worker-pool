#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include <vector>
#include <list>
#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>

using namespace worker_pool;

TEST(WaitAll, Empty) {
    pool pool(2);
    std::vector<task<void>> t;
    pool::wait_all(t);
}

TEST(WaitAll, Smart) {
    pool pool(2, 0, false);
    const auto startTime = std::chrono::steady_clock::now();
    std::mutex mutex;
    std::condition_variable cv;
    bool task1Started = false;
    pool.add("outer", [&] {
        std::vector<task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add("sub1", [&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
                cv.notify_one();
            }
            sleepMs(1000);
        }));

        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }

        auto sub2 = subtasks.emplace_back(pool.add("sub2", [] { sleepMs(1000); }));

        // This should do sub2 first instead of naively blocking on sub1.
        pool::wait_all(subtasks);
    }).wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_LT(durationMs, 1500);
}

TEST(WaitAll, Iterators) {
    pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        std::vector<task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool::wait_all(subtasks.begin(), subtasks.end());
    }).wait();
    EXPECT_EQ(2, done);
}

TEST(WaitAll, Vector) {
    pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        std::vector<task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool::wait_all(subtasks);
    }).wait();
    EXPECT_EQ(2, done);
}

TEST(WaitAll, Array) {
    pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        std::vector<task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool::wait_all(subtasks.data(), subtasks.size());
    }).wait();
    EXPECT_EQ(2, done);
}

TEST(WaitAll, Iterable) {
    pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        // Use std::list because it doesn't have operator[].
        std::list<task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool::wait_all(subtasks);
    }).wait();
    EXPECT_EQ(2, done);
}

TEST(WaitAll, FirstIsSlow) {
    pool pool(4, 0, false);
    std::atomic<int> done = 0;
    const auto startTime = std::chrono::steady_clock::now();
    pool.add([&] {
        std::list<task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] {
            sleepMs(1000);
            ++done;
        }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool::wait_all(subtasks);
    }).wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_GE(durationMs, 1000);
    EXPECT_LT(durationMs, 1500);
    EXPECT_EQ(2, done);
}

TEST(WaitAll, LastIsSlow) {
    pool pool(4, 0, false);
    std::atomic<int> done = 0;
    const auto startTime = std::chrono::steady_clock::now();
    pool.add([&] {
        std::list<task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] {
            sleepMs(1000);
            ++done;
        }));
        pool::wait_all(subtasks);
    }).wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_GE(durationMs, 1000);
    EXPECT_LT(durationMs, 1500);
    EXPECT_EQ(2, done);
}

TEST(WaitAll, OneSlower) {
    pool pool(4, 0, false);
    std::atomic<int> done = 0;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<int> tasksStarted = 0;
    const auto startTime = std::chrono::steady_clock::now();
    auto outer = pool.add("outer", [&] {
        std::list<task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add("sub1", [&] {
            {
                std::lock_guard lock(mutex);
                ++tasksStarted;
                cv.notify_one();
            }
            sleepMs(1000);
            ++done;
        }));
        auto sub2 = subtasks.emplace_back(pool.add("sub2", [&] {
            {
                std::lock_guard lock(mutex);
                ++tasksStarted;
                cv.notify_one();
            }
            sleepMs(500);
            ++done;
        }));
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return tasksStarted == 2; });
        }
        pool::wait_all(subtasks);
    });
    outer.wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_GE(durationMs, 1000);
    EXPECT_LT(durationMs, 1500);
    EXPECT_EQ(2, done);
}

TEST(WaitAll, OffPoolWork) {
    pool pool(1, 0, true);
    std::vector<task<void>> tasks;
    const auto startTime = std::chrono::steady_clock::now();
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    pool::wait_all(tasks);
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_GE(durationMs, 500);
    EXPECT_LT(durationMs, 1000);
}

TEST(WaitAll, NoOffPoolWork) {
    pool pool(1, 0, false);
    std::vector<task<void>> tasks;
    const auto startTime = std::chrono::steady_clock::now();
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    pool::wait_all(tasks);
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_GE(durationMs, 1000);
    EXPECT_LT(durationMs, 1500);
}
