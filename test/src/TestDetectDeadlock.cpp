#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include <stdexcept>

using namespace worker_pool;

void requireDeadlockDetection() {
#ifndef WORKER_POOL_DEADLOCK_DETECTION
    GTEST_SKIP() << "Don't have deadlock detection";
#endif
}


void requireStrictDeadlockDetection() {
#ifndef WORKER_POOL_DEADLOCK_DETECTION_STRICT
    GTEST_SKIP() << "Don't have strict deadlock detection";
#endif
}

TEST(Deadlock, Simple) {
    requireDeadlockDetection();
    pool pool(2, 0, false);
    task<void>* pt2 = nullptr;
    auto t1 = pool.add("t1", [&] {
        do
            sleepMs(100);
        while (!pt2);
        pt2->wait();
    });
    auto t2 = pool.add("t2", [&] { t1.wait(); });
    pt2 = &t2;
    t1.wait();
    t2.get();
    EXPECT_ANY_THROW(t1.get());
}

class WaitCycle {
    std::mutex mutex;
    std::condition_variable taskStarted;
    std::atomic<int> startNext = 0;
    std::vector<task<void>> tasks;
    int length;

    task<void> makeWaiter(pool& pool, int index) {
        const auto name = std::string("t") + std::to_string(index);
        return pool.add(name, [&, index, name] {
            printf("%s awaiting mutex\n", name.c_str());
            std::unique_lock lock(mutex);
            printf("%s awaiting cv\n", name.c_str());
            taskStarted.wait(lock, [&, index] {
                if (tasks.size() != static_cast<size_t>(length))
                    return false;

                int expected = index;
                int next = (expected + 1) % length;
                if (startNext.compare_exchange_strong(expected, next)) {
                    printf("%s Changed startNext from %d to %d\n", name.c_str(), expected, next);
                    return true;
                } else {
                    printf("%s still waiting for startNext == %d (currently %d)\n", name.c_str(), index, expected);
                }
                return false;
            });
            lock.unlock();
            taskStarted.notify_all();
            int toAwaitIdx = (index + 1) % length;
            auto& toAwait = tasks[toAwaitIdx];
            printf("%s awaiting task %s\n", name.c_str(), toAwait.get_name().c_str());

            toAwait.get();
        });
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

    task<void>& getFirst() {
        return tasks.front();
    }
};

TEST(Deadlock, Two) {
    requireDeadlockDetection();
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
    EXPECT_ANY_THROW({
        t1.get();
        t2.get();
        t3.get();
        });
}

TEST(Deadlock, Cycle1) {
    requireDeadlockDetection();
    pool pool(1, 0, false);
    WaitCycle cycle(pool, 1);
    auto& first = cycle.getFirst();
    EXPECT_ANY_THROW(first.get());
}

TEST(Deadlock, Cycle2) {
    requireDeadlockDetection();
    pool pool(2, 0, false);
    WaitCycle cycle(pool, 2);
    auto& first = cycle.getFirst();
    EXPECT_ANY_THROW(first.get());
}

TEST(Deadlock, Cycle3) {
    requireDeadlockDetection();
    pool pool(3, 0, false);
    WaitCycle cycle(pool, 3);
    auto& first = cycle.getFirst();
    EXPECT_ANY_THROW(first.get());
}

TEST(Deadlock, Cycle4) {
    requireDeadlockDetection();
    pool pool(4, 0, false);
    WaitCycle cycle(pool, 4);
    auto& first = cycle.getFirst();
    EXPECT_ANY_THROW(first.get());
}

TEST(Deadlock, Cycle40) {
    requireDeadlockDetection();
    pool pool(4, 0, false);
    WaitCycle cycle(pool, 40);
    auto& first = cycle.getFirst();
    EXPECT_ANY_THROW(first.get());
}
