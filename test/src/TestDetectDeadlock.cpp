#include "TestUtils.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;

void requireDeadlockDetection() {
#ifndef WORKER_POOL_DEADLOCK_DETECTION
    SUCCEED() << "Don't have deadlock detection";
#endif
}


void requireStrictDeadlockDetection() {
#ifndef WORKER_POOL_DEADLOCK_DETECTION_STRICT
    SUCCEED() << "Don't have strict deadlock detection";
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
    t1.wait();
    t2.get();
    t3.get();
    EXPECT_ANY_THROW(t1.get());
}
