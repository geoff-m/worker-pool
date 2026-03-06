#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include <chrono>

using namespace worker_pool;

TEST(BoundedQueue, Construct) {
    pool_builder builder;
    builder.setTargetParallelism(1);
    builder.setQueueSize(1);
    auto pool = builder.build();
}

TEST(BoundedQueue, Block) {
    pool_builder builder;
    builder.setTargetParallelism(1);
    builder.setQueueSize(1);
    std::atomic<int> finishedTasks = 0;
    constexpr auto TOTAL_TASK_COUNT = 10;
    {
        auto pool = builder.build();
        const auto startTime = std::chrono::steady_clock::now();
        for (int taskCount = 0; taskCount < TOTAL_TASK_COUNT; ++taskCount) {
            pool.add([&] {
                sleepMs(100);
                ++finishedTasks;
            });
        }
        const auto stopTime = std::chrono::steady_clock::now();
        const auto addDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - startTime);
        EXPECT_GT(addDurationMs.count(), 500);
    }
    EXPECT_EQ(TOTAL_TASK_COUNT, finishedTasks);
}

TEST(BoundedQueue, DropOld) {
    pool_builder builder;
    builder.setTargetParallelism(1);
    builder.setExtraThreads(0);
    builder.setQueueSize(1);
    builder.setFullQueuePolicy(FullQueuePolicy::DropOld);
    bool didTask1 = false;
    bool didTask2 = false;
    bool didTask3 = false;
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool task1Started = false;

        auto pool = builder.build();
        pool.add("task1", [&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
            }
            cv.notify_one();

            sleepMs(1000);
            didTask1 = true;
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        const auto startTime = std::chrono::steady_clock::now();
        // These two 'add' calls should finish right away.
        // task2 fills the queue.
        pool.add("task2", [&] { didTask2 = true; });

        // This should cause task2 to be dropped.
        pool.add("task3", [&] { didTask3 = true; });
        const auto stopTime = std::chrono::steady_clock::now();
        const auto addDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - startTime);
        EXPECT_LT(addDurationMs.count(), 100);
    }
    EXPECT_TRUE(didTask1);
    EXPECT_FALSE(didTask2);
    EXPECT_TRUE(didTask3);
}

TEST(BoundedQueue, DropNew) {
    pool_builder builder;
    builder.setTargetParallelism(1);
    builder.setExtraThreads(0);
    builder.setQueueSize(1);
    builder.setFullQueuePolicy(FullQueuePolicy::DropNew);
    bool didTask1 = false;
    bool didTask2 = false;
    bool didTask3 = false;
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool task1Started = false;

        auto pool = builder.build();
        pool.add("task1", [&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
            }
            cv.notify_one();

            sleepMs(1000);
            didTask1 = true;
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        const auto startTime = std::chrono::steady_clock::now();
        // These two 'add' calls should finish right away.
        // task2 fills the queue.
        pool.add("task2", [&] {
            sleepMs(1000);
            didTask2 = true;
        });

        // This should be dropped because the queue is already full.
        pool.add("task3", [&] { didTask3 = true; });
        const auto stopTime = std::chrono::steady_clock::now();
        const auto addDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - startTime);
        EXPECT_LT(addDurationMs.count(), 100);
    }
    EXPECT_TRUE(didTask1);
    EXPECT_TRUE(didTask2);
    EXPECT_FALSE(didTask3);
}
