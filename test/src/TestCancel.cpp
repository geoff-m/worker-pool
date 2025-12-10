#include "TestUtils.h"
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
        sleep(1);
        return 1;
    });
    auto t2 = pool.add([] {
        sleep(1);
        return 2;
    });
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return task1Started; });
    }
    pool.shutDown(true);

    EXPECT_EQ(1, t1.get());
    EXPECT_ANY_THROW((void)t2.get());
    EXPECT_ANY_THROW((void)t2.get());
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
        sleep(1);
        return 1;
    });
    auto t2 = pool.add([] { sleep(1); });
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return task1Started; });
    }
    pool.shutDown(true);

    EXPECT_EQ(1, t1.get());
    EXPECT_ANY_THROW(t2.get());
    EXPECT_ANY_THROW(t2.get());
}

TEST(Cancel, NoCancelAtShutdown) {
    pool pool(1, 0, false);
    auto t1 = pool.add([] {
        sleep(1);
        return 1;
    });
    auto t2 = pool.add([] {
        sleep(1);
        return 2;
    });
    pool.shutDown();
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
                cv.notify_one();
            }
            sleep(1);
            ++tasksDone;
            return 1;
        });
        auto t2 = pool.add("t2", [&] {
            sleep(1);
            ++tasksDone;
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        EXPECT_ANY_THROW(t2.get());
        EXPECT_EQ(1, t1.get());
        EXPECT_ANY_THROW(t2.get());
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
            sleep(1);
            ++tasksDone;
            return 1;
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        auto t2 = pool.add("t2", [&] {
            sleep(1);
            ++tasksDone;
            return 1;
        });
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        EXPECT_ANY_THROW((void)t2.get());
        EXPECT_EQ(1, t1.get());
        EXPECT_ANY_THROW((void)t2.get());
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
            sleep(1);
            ++tasksDone;
            return 1;
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        auto t2 = pool.add("t2", [&] {
            sleep(1);
            ++tasksDone;
            return 1;
        });
        auto waitTask = std::async(std::launch::async, [&] {
            return t2.get();
        });
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        EXPECT_ANY_THROW((void)waitTask.get());
        EXPECT_ANY_THROW((void)t2.get());
        EXPECT_EQ(1, t1.get());
        EXPECT_ANY_THROW((void)t2.get());
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
            sleep(1);
            ++tasksDone;
            return 1;
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        auto t2 = pool.add("t2", [&] {
            sleep(1);
            ++tasksDone;
        });
        auto waitTask = std::async(std::launch::async, [&] {
            return t2.get();
        });
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        EXPECT_ANY_THROW(waitTask.get());
        EXPECT_ANY_THROW(t2.get());
        EXPECT_EQ(1, t1.get());
        EXPECT_ANY_THROW(t2.get());
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
            sleep(1);
            ++tasksDone;
            return 1;
        });
        tasks.emplace_back(t1);
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        auto t2 = pool.add("t2", [&] {
            sleep(1);
            ++tasksDone;
            return 1;
        });
        tasks.emplace_back(t2);
        auto waitTask = std::async(std::launch::async, [&] {
            pool.wait_all(tasks);
        });
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        waitTask.get(); // Wait for the wait_all to finish.
        const auto endTime = std::chrono::steady_clock::now();
        const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        EXPECT_LT(durationMs, 1500);
        EXPECT_ANY_THROW((void)t2.get());
        EXPECT_EQ(1, t1.get());
        EXPECT_ANY_THROW((void)t2.get());
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
            sleep(1);
            ++tasksDone;
        });
        tasks.emplace_back(t1);
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return task1Started; });
        }
        auto t2 = pool.add("t2", [&] {
            sleep(1);
            ++tasksDone;
        });
        tasks.emplace_back(t2);
        auto waitTask = std::async(std::launch::async, [&] {
            pool.wait_all(tasks);
        });
        // t2 is unstarted, so canceling it should succeed.
        EXPECT_TRUE(t2.try_cancel());
        waitTask.get(); // Wait for the wait_all to finish.
        const auto endTime = std::chrono::steady_clock::now();
        const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        EXPECT_LT(durationMs, 1500);
        EXPECT_ANY_THROW(t2.get());
        EXPECT_NO_THROW(t1.get());
        EXPECT_ANY_THROW(t2.get());
    }
    EXPECT_EQ(1, tasksDone);
}


