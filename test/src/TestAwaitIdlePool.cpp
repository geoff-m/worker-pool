#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include <chrono>

using namespace worker_pool;

const auto tenMs = std::chrono::milliseconds(10);

TEST(AwaitIdlePool, Create1) {
    pool pool(1);
    pool.await_idle_pool();
}

TEST(AwaitIdlePool, Create4) {
    pool pool(4);
    pool.await_idle_pool();
}

TEST(AwaitIdlePool, WaitFor1) {
    pool pool(1);
    while (!pool.await_idle_pool_for(tenMs))
        std::this_thread::sleep_for(tenMs);
}

TEST(AwaitIdlePool, WaitFor4) {
    pool pool(4);
    while (!pool.await_idle_pool_for(tenMs))
        std::this_thread::sleep_for(tenMs);
}

TEST(AwaitIdlePool, WaitUntil1) {
    pool pool(1);
    while (true) {
        const auto deadline = std::chrono::steady_clock::now() + tenMs;
        if (pool.await_idle_pool_until(deadline))
            return;
        std::this_thread::sleep_for(tenMs);
    }
}

TEST(AwaitIdlePool, WaitUntil4) {
    pool pool(4);
    while (true) {
        const auto deadline = std::chrono::steady_clock::now() + tenMs;
        if (pool.await_idle_pool_until(deadline))
            return;
        std::this_thread::sleep_for(tenMs);
    }
}

TEST(AwaitIdlePool, Wait) {
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
    pool.await_idle_pool();
    EXPECT_TRUE(task.is_done());
}

TEST(AwaitIdlePool, WaitForTimeout) {
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
    EXPECT_FALSE(pool.await_idle_pool_for(std::chrono::milliseconds(500)));
}

TEST(AwaitIdlePool, WaitUntilTimeout) {
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
    EXPECT_FALSE(pool.await_idle_pool_until(deadline));
}

TEST(AwaitIdlePool, ReturnsAtShutDown) {
    constexpr auto TASK_TIME = std::chrono::milliseconds(2000);
    pool pool(1);
    auto task = pool.add([&] { std::this_thread::sleep_for(TASK_TIME); });
    pool.shutDown();
    pool.await_idle_pool();
}

TEST(AwaitIdlePool, ReturnsAtShutDownFor) {
    constexpr auto TASK_TIME = std::chrono::milliseconds(2000);
    pool pool(1);
    auto task = pool.add([&] { std::this_thread::sleep_for(TASK_TIME); });
    pool.shutDown();
    pool.await_idle_pool_for(TASK_TIME+ std::chrono::seconds(1));
}
