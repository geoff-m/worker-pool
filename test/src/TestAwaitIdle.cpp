#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include <chrono>

using namespace worker_pool;

const auto tenMs = std::chrono::milliseconds(10);

TEST(AwaitIdle, Create1) {
    pool pool(1);
    while (pool.await_idle_thread() < 1)
        std::this_thread::sleep_for(tenMs);
}

TEST(AwaitIdle, Create4) {
    pool pool(4);
    while (pool.await_idle_thread() < 4)
        std::this_thread::sleep_for(tenMs);
}

TEST(AwaitIdle, WaitFor1) {
    pool pool(1);
    while (pool.await_idle_thread_for(tenMs) < 1)
        std::this_thread::sleep_for(tenMs);
}

TEST(AwaitIdle, WaitFor4) {
    pool pool(4);
    while (pool.await_idle_thread_for(tenMs) < 4)
        std::this_thread::sleep_for(tenMs);
}

TEST(AwaitIdle, WaitUntil1) {
    pool pool(1);
    while (true) {
        const auto deadline = std::chrono::steady_clock::now() + tenMs;
        if (pool.await_idle_thread_until(deadline) == 1)
            return;
        std::this_thread::sleep_for(tenMs);
    }
}


TEST(AwaitIdle, WaitUntil4) {
    pool pool(4);
    while (true) {
        const auto deadline = std::chrono::steady_clock::now() + tenMs;
        if (pool.await_idle_thread_until(deadline) == 4)
            return;
        std::this_thread::sleep_for(tenMs);
    }
}

TEST(AwaitIdle, Wait) {
    std::atomic<bool> started = false;
    std::mutex mutex;
    std::condition_variable cv;
    pool pool(1);
    const auto task = pool.add([&] {
        {
            std::lock_guard lock(mutex);
            started.store(true, std::memory_order::release);
        }
        cv.notify_one();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    });
    // Wait for the task to be in progress before we call await_idle_thread.
    // Otherwise we might call too early, before the task has started.
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return started.load(std::memory_order::acquire); });
    }
    EXPECT_EQ(1, pool.await_idle_thread());
    EXPECT_TRUE(task.is_done());
}

TEST(AwaitIdle, WaitForTimeout) {
    std::atomic<bool> started = false;
    std::mutex mutex;
    std::condition_variable cv;
    pool pool(1);
    const auto task = pool.add([&] {
        {
            std::lock_guard lock(mutex);
            started.store(true, std::memory_order::release);
        }
        cv.notify_one();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    });
    // Wait for the task to be in progress before we call await_idle_thread.
    // Otherwise we might call too early, before the task has started.
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return started.load(std::memory_order::acquire); });
    }
    EXPECT_EQ(0, pool.await_idle_thread_for(std::chrono::milliseconds(500)));
}


TEST(AwaitIdle, WaitUntilTimeout) {
    std::atomic<bool> started = false;
    std::mutex mutex;
    std::condition_variable cv;
    pool pool(1);
    const auto task = pool.add([&] {
        {
            std::lock_guard lock(mutex);
            started.store(true, std::memory_order::release);
        }
        cv.notify_one();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    });
    // Wait for the task to be in progress before we call await_idle_thread.
    // Otherwise we might call too early, before the task has started.
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return started.load(std::memory_order::acquire); });
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    EXPECT_EQ(0, pool.await_idle_thread_until(deadline));
}

TEST(AwaitIdle, ReturnsAtShutDown) {
    constexpr auto TASK_TIME = std::chrono::milliseconds(2000);
    pool pool(1);
    auto task = pool.add([&] { std::this_thread::sleep_for(TASK_TIME); });
    pool.shutDown();
    const auto waitStartTime = std::chrono::steady_clock::now();
    while (true) {
        if (pool.await_idle_thread() == 0)
            return;
        const auto timeWaited = std::chrono::steady_clock::now() - waitStartTime;
        ASSERT_LE(timeWaited, TASK_TIME / 2);
        std::this_thread::sleep_for(tenMs);
    }
}

TEST(AwaitIdle, ReturnsAtShutDownFor) {
    constexpr auto TASK_TIME = std::chrono::milliseconds(2000);
    pool pool(1);
    auto task = pool.add([&] { std::this_thread::sleep_for(TASK_TIME); });
    pool.shutDown();
    const auto waitStartTime = std::chrono::steady_clock::now();
    while (true) {
        if (pool.await_idle_thread_for(std::chrono::milliseconds(10)) == 0)
            return;
        const auto timeWaited = std::chrono::steady_clock::now() - waitStartTime;
        ASSERT_LE(timeWaited, TASK_TIME / 2);
        std::this_thread::sleep_for(tenMs);
    }
}

TEST(AwaitIdle, Pipeline) {
    constexpr auto TASK_COUNT = 1000;
    std::atomic<int> doneTasks = 0;
    {
        constexpr auto THREAD_COUNT = 2;
        std::atomic<int> activeTasks = 0;
        pool pool(THREAD_COUNT);
        for (int i = 0; i < TASK_COUNT; i++) {
            const auto idleThreads = pool.await_idle_thread();
            EXPECT_TRUE(idleThreads >= 1 && idleThreads <= THREAD_COUNT);
            pool.add([&] {
                EXPECT_LE(++activeTasks, THREAD_COUNT);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                --activeTasks;
                ++doneTasks;
            });
        }
    }
    EXPECT_EQ(TASK_COUNT, doneTasks.load(std::memory_order::acquire));
}
