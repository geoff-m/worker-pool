#include "TestUtils.h"
#include "WorkerPool.h"

using namespace WorkerPool;
using namespace std::chrono;

TEST(TimedWait, VoidTimeout) {
    Pool pool(1);
    const auto testStart = steady_clock::now();
    auto task = pool.add([] { sleepMs(1000); });
    EXPECT_FALSE(task.wait(milliseconds(500)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, seconds(1));
}

TEST(TimedWait, VoidNoTimeout) {
    Pool pool(1);
    const auto testStart = steady_clock::now();
    auto task = pool.add([] { sleepMs(1000); });
    EXPECT_TRUE(task.wait(seconds(10)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, milliseconds(1500));
}

TEST(TimedWait, IntTimeout) {
    Pool pool(1);
    const auto testStart = steady_clock::now();
    auto task = pool.add([] { sleepMs(1000); return 1;});
    EXPECT_FALSE(task.wait(milliseconds(500)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, seconds(1));
}

TEST(TimedWait, IntNoTimeout) {
    Pool pool(1);
    const auto testStart = steady_clock::now();
    auto task = pool.add([] { sleepMs(1000); return 1; });
    EXPECT_TRUE(task.wait(seconds(10)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, milliseconds(1500));
}

TEST(TimedWait, WaitAllTimeout) {
    Pool pool(1);
    const auto testStart = steady_clock::now();
    std::vector<Task<void>> tasks;
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    EXPECT_FALSE(pool.waitAll(tasks, milliseconds(1250)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, milliseconds(1500));
}

TEST(TimedWait, WaitAllNoTimeout) {
    Pool pool(1);
    const auto testStart = steady_clock::now();
    std::vector<Task<void>> tasks;
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    EXPECT_TRUE(pool.waitAll(tasks, seconds(10)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, seconds(2));
}