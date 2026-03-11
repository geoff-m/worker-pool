#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include "FullPool.h"
#include <chrono>
#include <cstdio>

using namespace worker_pool;

TEST(BoundedQueue, Construct) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_max_queue_size(1);
    auto pool = builder.build();
}

void expectSetPolicyViaBuilder(FullQueuePolicy policy) {
    pool_builder builder;
    builder.set_full_queue_policy(policy);
    auto pool = builder.build();
    EXPECT_EQ(policy, pool.get_full_queue_policy());
}

TEST(BoundedQueue, SetPolicyBlock) {
 expectSetPolicyViaBuilder(FullQueuePolicy::Block);
}

TEST(BoundedQueue, SetPolicyDropOld) {
    expectSetPolicyViaBuilder(FullQueuePolicy::DropOld);
}

TEST(BoundedQueue, SetPolicyDropNew) {
    expectSetPolicyViaBuilder(FullQueuePolicy::DropNew);
}

TEST(BoundedQueue, Block) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_max_queue_size(1);
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
    builder.set_max_queue_size(1);
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
    builder.set_max_queue_size(1);
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

const auto SHORT_TIME = std::chrono::milliseconds(250);

template<class Rep, class Period>
auto getFuture(const std::chrono::duration<Rep, Period>& durationIntoFuture) {
    return std::chrono::steady_clock::now() + durationIntoFuture;
}


TEST(BoundedQueue, BlockTryAddVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_max_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::Block);
    bool taskStarted = false;
    {
        auto pool = builder.build();
        FullPool fq(pool);

        task<void> task;
        EXPECT_FALSE(pool.try_add(task, [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){ taskStarted = true; (void)arg; }, 5));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_for(task ,SHORT_TIME, "name", [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, "name", [&](int arg){ taskStarted = true; (void)arg; }, 5));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name", [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name",
            [&](int arg){ taskStarted = true; (void)arg; }, 5));
    }
    EXPECT_FALSE(taskStarted);
}

TEST(BoundedQueue, BlockTryAddNonVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_max_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::Block);
    bool taskStarted = false;
    {
        auto pool = builder.build();
        FullPool fq(pool);

        task<int> task;
        EXPECT_FALSE(pool.try_add(task, [&]{taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){ taskStarted = true; return arg; }, 5));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, [&]{taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, "name", [&]{ taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, "name", [&](int arg){ taskStarted = true; return arg; }, 5));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), [&]{taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name", [&]{ taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name",
                [&](int arg){ taskStarted = true; return arg; }, 5));
    }
    EXPECT_FALSE(taskStarted);
}

TEST(BoundedQueue, DropOldTryAddVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_max_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::DropOld);
    bool taskStarted = false;
    {
        auto pool = builder.build();
        FullPool fq(pool);

        task<void> task;
        EXPECT_FALSE(pool.try_add(task, [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){ taskStarted = true; (void)arg; }, 5));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_for(task ,SHORT_TIME, "name", [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, "name", [&](int arg){ taskStarted = true; (void)arg; }, 5));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name", [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name",
            [&](int arg){ taskStarted = true; (void)arg; }, 5));
    }
    EXPECT_FALSE(taskStarted);
}

TEST(BoundedQueue, DropOldTryAddNonVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_max_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::DropOld);
    bool taskStarted = false;
    {
        auto pool = builder.build();
        FullPool fq(pool);

        task<int> task;
        EXPECT_FALSE(pool.try_add(task, [&]{taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){ taskStarted = true; return arg; }, 5));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, [&]{taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, "name", [&]{ taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, "name", [&](int arg){ taskStarted = true; return arg; }, 5));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), [&]{taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name", [&]{ taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name",
                [&](int arg){ taskStarted = true; return arg; }, 5));
    }
    EXPECT_FALSE(taskStarted);
}

TEST(BoundedQueue, DropNewTryAddVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_max_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::DropNew);
    bool taskStarted = false;
    {
        auto pool = builder.build();
        FullPool fq(pool);

        task<void> task;
        EXPECT_FALSE(pool.try_add(task, [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){ taskStarted = true; (void)arg; }, 5));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_for(task ,SHORT_TIME, "name", [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, "name", [&](int arg){ taskStarted = true; (void)arg; }, 5));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name", [&]{ taskStarted = true; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name",
            [&](int arg){ taskStarted = true; (void)arg; }, 5));
    }
    EXPECT_FALSE(taskStarted);
}

TEST(BoundedQueue, DropNewTryAddNonVoid) {
    pool_builder builder;
    builder.set_target_parallelism(1);
    builder.set_extra_threads(0);
    builder.set_max_queue_size(1);
    builder.set_full_queue_policy(FullQueuePolicy::DropNew);
    bool taskStarted = false;
    {
auto pool = builder.build();
        FullPool fq(pool);

        task<int> task;
        EXPECT_FALSE(pool.try_add(task, [&]{taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&]{ taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add(task, "name", [&](int arg){ taskStarted = true; return arg; }, 5));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, [&]{taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, "name", [&]{ taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_for(task, SHORT_TIME, "name", [&](int arg){ taskStarted = true; return arg; }, 5));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), [&]{taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name", [&]{ taskStarted = true; return 123; }));
        EXPECT_FALSE(pool.try_add_until(task, getFuture(SHORT_TIME), "name",
            [&](int arg){ taskStarted = true; return arg; }, 5));
    }
    EXPECT_FALSE(taskStarted);
}