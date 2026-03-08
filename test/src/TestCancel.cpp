#include "TestUtils.h"
#include "FullPool.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;

TEST(Cancel, CancelNonvoidAtShutdown) {
    pool pool(1, 0, false);
    std::mutex mutex;
    std::condition_variable cv;
    bool task1Started = false;
    auto t1 = pool.add([&] {
        {
            std::lock_guard lock(mutex);
            task1Started = true;
            cv.notify_one();
        }
        sleepMs(1000);
        return 1;
    });
    auto t2 = pool.add([] {
        sleepMs(1000);
        return 2;
    });
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return task1Started; });
    }
    pool.shut_down(true);

    EXPECT_EQ(1, t1.get());
    EXPECT_THROW((void)t2.get(), canceled_exception);
    EXPECT_THROW((void)t2.get(), canceled_exception);
}

TEST(Cancel, CancelVoidAtShutdown) {
    pool pool(1, 0, false);
    std::mutex mutex;
    std::condition_variable cv;
    bool task1Started = false;
    auto t1 = pool.add([&] {
        {
            std::lock_guard lock(mutex);
            task1Started = true;
            cv.notify_one();
        }
        sleepMs(1000);
        return 1;
    });
    auto t2 = pool.add([] { sleepMs(1000); });
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return task1Started; });
    }
    pool.shut_down(true);

    EXPECT_EQ(1, t1.get());
    EXPECT_THROW(t2.get(), canceled_exception);
    EXPECT_THROW(t2.get(), canceled_exception);
}

TEST(Cancel, NoCancelAtShutdown) {
    pool pool(1, 0, false);
    auto t1 = pool.add([] {
        sleepMs(1000);
        return 1;
    });
    auto t2 = pool.add([] {
        sleepMs(1000);
        return 2;
    });
    pool.shut_down();
    EXPECT_EQ(1, t1.get());
    EXPECT_EQ(2, t2.get());
}

TEST(Cancel, CancelUnstartedVoid) {
    std::atomic<int> tasksDone = 0;
    {
        pool pool(1, 0, false);
        std::mutex mutex;
        std::condition_variable cv;
        bool task1Started = false;
        auto t1 = pool.add("t1", [&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
            }
            cv.notify_one();
            sleepMs(1000);
            ++tasksDone;
            return 1;
        });
        auto t2 = pool.add("t2", [&] {
            sleepMs(1000);
            ++tasksDone;
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        EXPECT_THROW(t2.get(), canceled_exception);
        EXPECT_EQ(1, t1.get());
        EXPECT_THROW(t2.get(), canceled_exception);
    }
    EXPECT_EQ(1, tasksDone);
}

TEST(Cancel, CancelUnstartedNonvoid) {
    std::atomic<int> tasksDone = 0;
    {
        pool pool(1, 0, false);
        std::mutex mutex;
        std::condition_variable cv;
        bool task1Started = false;
        auto t1 = pool.add("t1", [&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
                cv.notify_one();
            }
            sleepMs(1000);
            ++tasksDone;
            return 1;
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        auto t2 = pool.add("t2", [&] {
            sleepMs(1000);
            ++tasksDone;
            return 1;
        });
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        EXPECT_THROW((void)t2.get(), canceled_exception);
        EXPECT_EQ(1, t1.get());
        EXPECT_THROW((void)t2.get(), canceled_exception);
    }
    EXPECT_EQ(1, tasksDone);
}

TEST(Cancel, CancelNonvoidDuringWait) {
    std::atomic<int> tasksDone = 0;
    {
        pool pool(1, 0, false);
        std::mutex mutex;
        std::condition_variable cv;
        bool task1Started = false;
        auto t1 = pool.add("t1", [&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
                cv.notify_one();
            }
            sleepMs(1000);
            ++tasksDone;
            return 1;
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        auto t2 = pool.add("t2", [&] {
            sleepMs(1000);
            ++tasksDone;
            return 1;
        });
        auto waitTask = std::async(std::launch::async, [&] {
            return t2.get();
        });
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        EXPECT_THROW((void)waitTask.get(), canceled_exception);
        EXPECT_THROW((void)t2.get(), canceled_exception);
        EXPECT_EQ(1, t1.get());
        EXPECT_THROW((void)t2.get(), canceled_exception);
    }
    EXPECT_EQ(1, tasksDone);
}

TEST(Cancel, CancelVoidDuringWait) {
    std::atomic<int> tasksDone = 0;
    {
        pool pool(1, 0, false);
        std::mutex mutex;
        std::condition_variable cv;
        bool task1Started = false;
        auto t1 = pool.add("t1", [&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
                cv.notify_one();
            }
            sleepMs(1000);
            ++tasksDone;
            return 1;
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        auto t2 = pool.add("t2", [&] {
            sleepMs(1000);
            ++tasksDone;
        });
        auto waitTask = std::async(std::launch::async, [&] {
            return t2.get();
        });
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        EXPECT_THROW(waitTask.get(), canceled_exception);
        EXPECT_THROW(t2.get(), canceled_exception);
        EXPECT_EQ(1, t1.get());
        EXPECT_THROW(t2.get(), canceled_exception);
    }
    EXPECT_EQ(1, tasksDone);
}

TEST(Cancel, CancelNonvoidDuringWaitAll) {
    std::atomic<int> tasksDone = 0;
    {
        pool pool(1, 0, false);
        std::mutex mutex;
        std::condition_variable cv;
        bool task1Started = false;
        std::vector<task<int>> tasks;
        const auto startTime = std::chrono::steady_clock::now();
        auto t1 = pool.add("t1", [&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
                cv.notify_one();
            }
            sleepMs(1000);
            ++tasksDone;
            return 1;
        });
        tasks.emplace_back(t1);
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        auto t2 = pool.add("t2", [&] {
            sleepMs(1000);
            ++tasksDone;
            return 1;
        });
        tasks.emplace_back(t2);
        auto waitTask = std::async(std::launch::async, [&] {
            pool::wait_all(tasks);
        });
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        waitTask.get(); // Wait for the wait_all to finish.
        const auto endTime = std::chrono::steady_clock::now();
        const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        EXPECT_LT(durationMs, 1500);
        EXPECT_THROW((void)t2.get(), canceled_exception);
        EXPECT_EQ(1, t1.get());
        EXPECT_THROW((void)t2.get(), canceled_exception);
    }
    EXPECT_EQ(1, tasksDone);
}

TEST(Cancel, CancelVoidDuringWaitAll) {
    std::atomic<int> tasksDone = 0;
    {
        pool pool(1, 0, false);
        std::mutex mutex;
        std::condition_variable cv;
        bool task1Started = false;
        std::vector<task<void>> tasks;
        const auto startTime = std::chrono::steady_clock::now();
        auto t1 = pool.add("t1", [&] {
            {
                std::lock_guard lock(mutex);
                task1Started = true;
                cv.notify_one();
            }
            sleepMs(1000);
            ++tasksDone;
        });
        tasks.emplace_back(t1);
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        auto t2 = pool.add("t2", [&] {
            sleepMs(1000);
            ++tasksDone;
        });
        tasks.emplace_back(t2);
        auto waitTask = std::async(std::launch::async, [&] {
            pool::wait_all(tasks);
        });
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        waitTask.get(); // Wait for the wait_all to finish.
        const auto endTime = std::chrono::steady_clock::now();
        const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        EXPECT_LT(durationMs, 1500);
        EXPECT_THROW(t2.get(), canceled_exception);
        EXPECT_NO_THROW(t1.get());
        EXPECT_THROW(t2.get(), canceled_exception);
    }
    EXPECT_EQ(1, tasksDone);
}

TEST(Cancel, CancelAlreadyCanceled) {
    pool pool(1);
    {
        FullPool fq(pool, false);
        auto t = pool.add("t1", [] {
        });
        EXPECT_TRUE(t.try_cancel());
        EXPECT_FALSE(t.try_cancel());
    }
}

TEST(Cancel, CancelAlreadyDone) {
    pool pool(1);
    auto t = pool.add("t1", [] {
    });
    t.wait();
    EXPECT_FALSE(t.try_cancel());
}

TEST(Cancel, CancelExecuting) {
    pool pool(1);
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    auto t = pool.add("t1", [&] {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return done; });
    });
    while (!t.is_executing())
        std::this_thread::yield();
    EXPECT_FALSE(t.try_cancel());
    done = true;
    cv.notify_one();
    t.wait();
}
