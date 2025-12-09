#include "TestUtils.h"
#include "../../include/worker-pool/worker-pool.h"
#include <vector>
#include <list>
#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>

using namespace worker_pool;

TEST(WaitAll, Empty) {
    Pool pool(2);
    std::vector<Task<void>> t;
    pool.wait_all(t);
}

TEST(WaitAll, Smart) {
    Pool pool(2, 0, false);
    const auto startTime = std::chrono::steady_clock::now();
    std::mutex mutex;
    std::condition_variable cv;
    bool task1Started = false;
    pool.add("outer", [&] {
        std::vector<Task<void>> subtasks;
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
        pool.wait_all(subtasks);
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
        pool.wait_all(subtasks.begin(), subtasks.end());
    }).wait();
    EXPECT_EQ(2, done);
}

TEST(WaitAll, Vector) {
    Pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        std::vector<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool.wait_all(subtasks);
    }).wait();
    EXPECT_EQ(2, done);
}

TEST(WaitAll, Array) {
    Pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        std::vector<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool.wait_all(subtasks.data(), subtasks.size());
    }).wait();
    EXPECT_EQ(2, done);
}

TEST(WaitAll, Iterable) {
    Pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        // Use std::list because it doesn't have operator[].
        std::list<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool.wait_all(subtasks);
    }).wait();
    EXPECT_EQ(2, done);
}

TEST(WaitAll, FirstIsSlow) {
    Pool pool(4, 0, false);
    std::atomic<int> done = 0;
    const auto startTime = std::chrono::steady_clock::now();
    pool.add([&] {
        std::list<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] {
            sleepMs(1000);
            ++done;
        }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool.wait_all(subtasks);
    }).wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_GE(durationMs, 1000);
    EXPECT_LT(durationMs, 1500);
    EXPECT_EQ(2, done);
}

TEST(WaitAll, LastIsSlow) {
    Pool pool(4, 0, false);
    std::atomic<int> done = 0;
    const auto startTime = std::chrono::steady_clock::now();
    pool.add([&] {
        std::list<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] {
            sleepMs(1000);
            ++done;
        }));
        pool.wait_all(subtasks);
    }).wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_GE(durationMs, 1000);
    EXPECT_LT(durationMs, 1500);
    EXPECT_EQ(2, done);
}

TEST(WaitAll, OneSlower) {
    Pool pool(4, 0, false);
    std::atomic<int> done = 0;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<int> tasksStarted = 0;
    const auto startTime = std::chrono::steady_clock::now();
    auto outer = pool.add("outer", [&] {
        std::list<Task<void>> subtasks;
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
        pool.wait_all(subtasks);
    });
    outer.wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_GE(durationMs, 1000);
    EXPECT_LT(durationMs, 1500);
    EXPECT_EQ(2, done);
}
