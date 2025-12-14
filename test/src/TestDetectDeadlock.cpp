#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include <stdexcept>

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
    std::condition_variable taskStarted;
    std::atomic<task<void>*> pt2 = nullptr;
    auto t1 = pool.add("t1", [&] {
        std::unique_lock lock(mutex);
        taskStarted.wait(lock);
        task<void>* loaded;
        while (nullptr == (loaded = pt2.load(std::memory_order::acquire))) {
            std::this_thread::yield();
        }
        loaded->wait();
    });
    auto t2 = pool.add("t2", [&] {
        taskStarted.notify_one();
        t1.wait();
    });
    pt2.store(&t2, std::memory_order::release);
    t1.wait();
    EXPECT_THROW({
                 t1.get();
                 t2.get();
                 }, deadlock_exception);
}

TEST(Deadlock, SimpleAbort) {
    REQUIRE_DEADLOCK_FATAL;
    EXPECT_DEATH({
                pool pool(2, 0, false);
                std::mutex mutex;
                std::condition_variable taskStarted;
                std::atomic<task<void>*> pt2 = nullptr;
                auto t1 = pool.add("t1", [&] {
                    std::unique_lock lock(mutex);
                    taskStarted.wait(lock);
                    task<void>* loaded;
                    while (nullptr == (loaded = pt2.load(std::memory_order::acquire))) {
                    std::this_thread::yield();
                    }
                    loaded->wait();
                    });
                auto t2 = pool.add("t2", [&] {
                    taskStarted.notify_one();
                    t1.wait();
                    });
                pt2.store(&t2, std::memory_order::release);
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
    std::atomic<int> startNext = 0;
    std::vector<std::shared_ptr<task<void>>> tasks;
    int length;

    std::shared_ptr<task<void>> makeWaiter(pool& pool, int index) {
        const auto name = std::string("t") + std::to_string(index);
        return std::make_shared<task<void>>(pool.add(name, [&, index, name] {
            std::unique_lock lock(mutex);
            taskStarted.wait(lock, [&, index] {
                if (tasks.size() != static_cast<size_t>(length))
                    return false;
                int expected = index;
                int next = (expected + 1) % length;
                return startNext.compare_exchange_strong(expected, next);
            });
            lock.unlock();
            taskStarted.notify_all();
            int toAwaitIdx = (index + 1) % length;
            auto toAwait = tasks[toAwaitIdx];
            toAwait->get();
        }));
    }

public:
    explicit WaitCycle(pool& pool, int length) : length(length) {
        if (length <= 0)
            throw std::invalid_argument("Length must be positive");

        for (int i = 0; i < length; i++) {
            tasks.emplace_back(makeWaiter(pool, i));
        }

        taskStarted.notify_all();
    }

    std::shared_ptr<task<void>> getFirst() {
        return tasks.front();
    }
};

TEST(Deadlock, Cycle1) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(1, 0, false);
    WaitCycle cycle(pool, 1);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
}

TEST(Deadlock, Cycle2) {
    REQUIRE_DEADLOCK_THROW;
    pool pool(2, 0, false);
    WaitCycle cycle(pool, 2);
    auto first = cycle.getFirst();
    EXPECT_THROW(first->get(), deadlock_exception);
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
