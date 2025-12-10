#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
using namespace worker_pool;

TEST(Multipool, WaitAllOtherPool) {
    pool p1(1);
    pool p2(1);
    std::vector<task<void>> tasks;
    tasks.emplace_back(p1.add([] { sleep(1); }));
    pool::wait_all(tasks);
}

TEST(Multipool, WaitAllMixOtherPool) {
    pool p1(1, 0, false);
    pool p2(1, 0, false);
    std::vector<task<void>> tasks;
    const auto startTime = std::chrono::steady_clock::now();
    tasks.emplace_back(p1.add([] { sleep(1); }));
    tasks.emplace_back(p2.add([] { sleep(1); }));
    pool::wait_all(tasks);
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_LT(durationMs, 1500);
}

TEST(Multipool, WaitOtherTask) {
    pool p1(1, 0, false);
    pool p2(1, 0, false);
    const auto startTime = std::chrono::steady_clock::now();
    auto t1 = p1.add([] { sleep(1); });
    auto t2 = p2.add([&] { t1.wait(); });
    t2.wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_GE(durationMs, 1000);
    EXPECT_LT(durationMs, 1500);
}

TEST(Multipool, WaitAllOtherTask) {
    pool p1(1, 0, false);
    pool p2(1, 0, false);
    const auto startTime = std::chrono::steady_clock::now();
    std::vector<task<void>> toWait;
    toWait.emplace_back(p1.add("t1", [] { sleepMs(500); }));
    toWait.emplace_back(p1.add("t2", [] { sleepMs(500); }));
    auto t3 = p2.add("t3", [&] { pool::wait_all(toWait); });
    t3.wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_GE(durationMs, 1000);
    EXPECT_LT(durationMs, 1500);
}

TEST(Multipool, WaitAllMixOtherTask) {
    pool p1(1, 0, false);
    pool p2(1, 0, false);
    const auto startTime = std::chrono::steady_clock::now();
    std::vector<task<void>> toWait;
    toWait.emplace_back(p1.add("t1", [] { sleepMs(500); }));
    toWait.emplace_back(p2.add("t2", [] { sleepMs(500); }));
    auto t3 = p2.add("t3", [&] { pool::wait_all(toWait); });
    t3.wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_GE(durationMs, 500);
    EXPECT_LT(durationMs, 1000);
}
