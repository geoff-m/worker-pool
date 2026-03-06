#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include <chrono>
#include <cstdio>

using namespace worker_pool;

TEST(BoundedQueue, Construct) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_queue_size(1);
    auto pool = builder.build();
}

TEST(BoundedQueue, Block) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_queue_size(1);
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
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::DropOld);
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
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::DropNew);
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

class FullQueue {
    std::mutex mutex;
    std::condition_variable cv;

    enum class State {
        FILLING,
        FULL,
        EXITING
    };

    State state;
    worker_pool::pool& pool;

public:
    explicit FullQueue(worker_pool::pool& pool)
        : state(State::FILLING),
          pool(pool) {
        const auto tasksToCreate = pool.get_target_parallelism() + pool.get_queue_size();
        for (unsigned int i = 1; i <= tasksToCreate; ++i) {
            pool.add([&] {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] {
                    return state == State::EXITING;
                });
            });
        }
    }

    ~FullQueue() {
        release();
    }

private:
    bool isFull() {
        task<void> t;
        return !pool.try_add(t, [] {
        });
    }

    void release() {
        std::lock_guard lock(mutex);
        {
            state = State::EXITING;
        }
        cv.notify_all();
    }
};

TEST(BoundedQueue, BlockTryAddVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::Block);
    {
        auto pool = builder.build();
        FullQueue fq(pool);

        task<void> task;
        EXPECT_FALSE(pool.try_add(task, []{}));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){ (void)arg; }, 5));
    }
}

TEST(BoundedQueue, BlockTryAddNonVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::Block);
    {
        auto pool = builder.build();
        FullQueue fq(pool);

        task<int> task;
        EXPECT_FALSE(pool.try_add(task, []{ return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){ return arg; }, 5));
    }
}

TEST(BoundedQueue, DropOldTryAddVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::DropOld);
    bool task1Started = false;
    bool task2Started = false;
    bool task3Started = false;
    {
        std::mutex mutex;
        std::condition_variable cv;
        auto pool = builder.build();
        pool.add([&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
            }
            cv.notify_one();
            sleepMs(1000);
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        pool.add([&] {
            task2Started = true;
        });

        task<void> task;
        EXPECT_FALSE(pool.try_add(task, [&]{ task3Started = true; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ task3Started = true; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){ task3Started = true; (void)arg; }, 5));
    }
    EXPECT_TRUE(task1Started);
    EXPECT_TRUE(task2Started);
    EXPECT_FALSE(task3Started);
}


TEST(BoundedQueue, DropOldTryAddNonVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::DropOld);
    bool task1Started = false;
    bool task2Started = false;
    bool task3Started = false;
    {
        std::mutex mutex;
        std::condition_variable cv;
        auto pool = builder.build();
        pool.add([&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
            }
            cv.notify_one();
            sleepMs(1000);
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        pool.add([&] {
            task2Started = true;
        });

        task<int> task;
        EXPECT_FALSE(pool.try_add(task, [&]{task3Started = true; return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ task3Started = true; return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){ task3Started = true; return arg; }, 5));
    }
    EXPECT_TRUE(task1Started);
    EXPECT_TRUE(task2Started);
    EXPECT_FALSE(task3Started);
}

TEST(BoundedQueue, DropNewTryAddVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::DropNew);
    bool task1Started = false;
    bool task2Started = false;
    bool task3Started = false;
    {
        std::mutex mutex;
        std::condition_variable cv;
        auto pool = builder.build();
        pool.add([&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
            }
            cv.notify_one();
            sleepMs(1000);
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        pool.add([&] {
            task2Started = true;
        });

        task<void> task;
        EXPECT_FALSE(pool.try_add(task, [&]{ task3Started = true; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ task3Started = true; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){task3Started = true; (void)arg; }, 5));
    }
    EXPECT_TRUE(task1Started);
    EXPECT_TRUE(task2Started);
    EXPECT_FALSE(task3Started);
}

TEST(BoundedQueue, DropNewTryAddNonVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::DropNew);
    bool task1Started = false;
    bool task2Started = false;
    bool task3Started = false;
    {
        std::mutex mutex;
        std::condition_variable cv;
        auto pool = builder.build();
        pool.add([&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
            }
            cv.notify_one();
            sleepMs(1000);
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        pool.add([&] {
            task2Started = true;
        });

        task<int> task;
        EXPECT_FALSE(pool.try_add(task, [&]{ task3Started = true; return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ task3Started = true; return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){ task3Started = true; return arg; }, 5));
    }
    EXPECT_TRUE(task1Started);
    EXPECT_TRUE(task2Started);
    EXPECT_FALSE(task3Started);
}
