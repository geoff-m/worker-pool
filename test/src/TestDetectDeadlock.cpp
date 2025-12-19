#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include <stdexcept>
#include <random>
#include <cstdio>

using namespace worker_pool;

#if defined(WORKER_POOL_DEADLOCK_DETECTION) && !defined(WORKER_POOL_DEADLOCK_DETECTION_FATAL)
#define REQUIRE_DEADLOCK_THROW
#else
#define REQUIRE_DEADLOCK_THROW do { GTEST_SKIP() << "Deadlocks don't throw"; } while (0)
#endif

#if defined(WORKER_POOL_DEADLOCK_DETECTION) && defined(WORKER_POOL_DEADLOCK_DETECTION_FATAL)
#define REQUIRE_DEADLOCK_FATAL
#else
#define REQUIRE_DEADLOCK_FATAL do { GTEST_SKIP() << "Deadlocks aren't fatal"; } while (0)
#endif

TEST(Deadlock, SimpleThrow) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(2, 0, false);
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<task<void>*> pt2 = nullptr;
    auto t1 = pool.add("t1", [&] {
        task<void>* loaded;
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return nullptr != (loaded = pt2.load(std::memory_order::acquire)); });
        }
        loaded->wait();
    });
    auto t2 = pool.add("t2", [&] {
        t1.wait();
    });
    {
        std::lock_guard lock(mutex);
        pt2.store(&t2, std::memory_order::release);
    }
    cv.notify_one();
    t1.wait();
    EXPECT_THROW({
                 t1.get();
                 t2.get();
                 }, deadlock_exception);
}

TEST(Deadlock, SimpleFatal) {
    REQUIRE_DEADLOCK_FATAL;
    EXPECT_DEATH({
                 pool pool(2, 0, false);
                 std::mutex mutex;
                 std::condition_variable cv;
                 std::atomic<task<void>*> pt2 = nullptr;
                 auto t1 = pool.add("t1", [&] {
                     task<void>* loaded;
                     {
                     std::unique_lock lock(mutex);
                     cv.wait(lock, [&] { return nullptr != (loaded = pt2.load(std::memory_order::acquire)); });
                     }
                     loaded->wait();
                     });
                 auto t2 = pool.add("t2", [&] {
                     t1.wait();
                     });
                 {
                 std::lock_guard lock(mutex);
                 pt2.store(&t2, std::memory_order::release);
                 }
                 cv.notify_one();
                 t1.wait();
                 t1.get();
                 t2.get();
                 }, ".*");
}

TEST(Deadlock, Two) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(3, 0, false);
    task<void>* pt2 = nullptr;
    task<void>* pt3 = nullptr;
    auto t1 = pool.add("t1", [&] {
        do
            sleepMs(100);
        while (!pt2);
        pt2->wait();
    });
    auto t2 = pool.add("t2", [&] {
        do
            sleepMs(100);
        while (!pt3);
        pt3->wait();
    });
    pt2 = &t2;
    auto t3 = pool.add("t3", [&] { t1.wait(); });
    pt3 = &t3;
    EXPECT_THROW({
                 t1.get();
                 t2.get();
                 t3.get();
                 }, deadlock_exception);
}

class WaitCycle {
    std::mutex mutex;
    std::condition_variable taskStarted;
    std::vector<std::shared_ptr<task<void>>> tasks;
    bool allTasksCreated = false;

    std::shared_ptr<task<void>> makeWaiter(pool& pool, int index, int waitForIndex) {
        const auto name = std::string("t") + std::to_string(index);
        return std::make_shared<task<void>>(pool.add(name, [&, waitForIndex, name] {
            std::unique_lock lock(mutex);
            taskStarted.wait(lock, [&] { return allTasksCreated; });
            lock.unlock();
            auto toAwait = tasks[waitForIndex];
            printf("%s will wait for %s\n", name.c_str(), toAwait->get_name().c_str());
            toAwait->get();
        }));
    }

public:
    explicit WaitCycle(pool& pool, int length) {
        if (length <= 0)
            throw std::invalid_argument("Length must be positive");

        // Create tasks 0..length-1 where task i waits for task i+1.
        // However, we'll create them (add them to the pool) in a random order.
        // Otherwise, these tests are boring.
        auto indices = std::unique_ptr<int[]>(new int[length]);
        auto inverseIndices = std::unique_ptr<int[]>(new int[length]);
        for (int i = 0; i < length; i++) {
            indices[i] = i;
            inverseIndices[i] = i;
        }
        // Randomize order that we create tasks in.
        // NOLINTNEXTLINE(cert-msc51-cpp)
        std::mt19937 engine(1337);
        std::uniform_int_distribution<int> dist(0, length - 1);
        for (int i = 0; i < length; i++) {
            const auto r = dist(engine);
            std::swap(indices[i], indices[r]);
        }
        for (int i = 0; i < length; i++) {
            inverseIndices[indices[i]] = i;
        }

        tasks.resize(length);
        for (int i = 0; i < length; i++) {
            tasks[i] = makeWaiter(pool, indices[i], inverseIndices[(indices[i] + 1) % length]);
        }

        {
            std::lock_guard lock(mutex);
            allTasksCreated = true;
        }
        taskStarted.notify_all();
    }

    std::shared_ptr<task<void>> getFirst() {
        return tasks[0];
    }
};

TEST(Deadlock, Cycle1) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(1, 0, false);
    WaitCycle cycle(pool, 1);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
}

TEST(Deadlock, Cycle1Fatal) {
    REQUIRE_DEADLOCK_FATAL;
    EXPECT_DEATH({ pool pool(1, 0, false);
                 WaitCycle cycle(pool, 1);
                 auto first = cycle.getFirst();
                 first->get();
                 }, ".*");
}

TEST(Deadlock, Cycle2) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(2, 0, false);
    WaitCycle cycle(pool, 2);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
}

TEST(Deadlock, Cycle2Fatal) {
    REQUIRE_DEADLOCK_FATAL;
    EXPECT_DEATH({ pool pool(2, 0, false);
                 WaitCycle cycle(pool, 2);
                 auto first = cycle.getFirst();
                 first->get();
                 }, ".*");
}

TEST(Deadlock, Cycle3) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(3, 0, false);
    WaitCycle cycle(pool, 3);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
}

TEST(Deadlock, Cycle4) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(4, 0, false);
    WaitCycle cycle(pool, 4);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
}

TEST(Deadlock, Cycle40) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(4, 0, false);
    WaitCycle cycle(pool, 40);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
}

TEST(Deadlock, Cycle80) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(4, 0, false);
    WaitCycle cycle(pool, 80);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
}

TEST(Deadlock, Cycle160) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(4, 0, false);
    WaitCycle cycle(pool, 160);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
}

TEST(Deadlock, Cycle320) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(4, 0, false);
    WaitCycle cycle(pool, 320);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
}

TEST(Deadlock, Cycle640) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(16, 0, false);
    WaitCycle cycle(pool, 640);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
}

TEST(Deadlock, Cycle1280) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(16, 0, false);
    WaitCycle cycle(pool, 1280);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
}
