#include "TestUtils.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;
using namespace std::chrono;

template<typename Rep, typename Period>
[[nodiscard]] auto nowPlus(duration<Rep, Period> duration) {
    return steady_clock::now() + duration;
}

TEST(TimedWait, VoidTimeoutFor) {
    pool pool(1);
    const auto testStart = steady_clock::now();
    auto task = pool.add([] { sleepMs(1000); });
    EXPECT_FALSE(task.wait_for(milliseconds(500)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, seconds(1));
}

TEST(TimedWait, VoidTimeoutUntil) {
    pool pool(1);
    const auto testStart = steady_clock::now();
    auto task = pool.add([] { sleepMs(1000); });
    EXPECT_FALSE(task.wait_until(nowPlus(milliseconds(500))));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, seconds(1));
}

TEST(TimedWait, VoidNoTimeout) {
    pool pool(1);
    const auto testStart = steady_clock::now();
    auto task = pool.add([] { sleepMs(1000); });
    EXPECT_TRUE(task.wait_for(seconds(10)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, milliseconds(1500));
}

TEST(TimedWait, IntTimeoutFor) {
    pool pool(1);
    const auto testStart = steady_clock::now();
    auto task = pool.add([] { sleepMs(1000); return 1;});
    EXPECT_FALSE(task.wait_for(milliseconds(500)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, seconds(1));
}

TEST(TimedWait, IntTimeoutUntil) {
    pool pool(1);
    const auto testStart = steady_clock::now();
    auto task = pool.add([] { sleepMs(1000); return 1;});
    EXPECT_FALSE(task.wait_until(nowPlus(milliseconds(500))));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, seconds(1));
}


TEST(TimedWait, IntNoTimeout) {
    pool pool(1);
    const auto testStart = steady_clock::now();
    auto task = pool.add([] { sleepMs(1000); return 1; });
    EXPECT_TRUE(task.wait_for(seconds(10)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, milliseconds(1500));
}

TEST(TimedWait, WaitAllTimeoutFor) {
    pool pool(1);
    const auto testStart = steady_clock::now();
    std::vector<task<void>> tasks;
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    EXPECT_FALSE(pool.wait_all_for(tasks, milliseconds(1250)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, milliseconds(1500));
}

TEST(TimedWait, WaitAllTimeoutUntil) {
    pool pool(1);
    const auto testStart = steady_clock::now();
    std::vector<task<void>> tasks;
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    EXPECT_FALSE(pool.wait_all_until(tasks, nowPlus(milliseconds(1250))));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, milliseconds(1500));
}

TEST(TimedWait, WaitAllNoTimeout) {
    pool pool(1);
    const auto testStart = steady_clock::now();
    std::vector<task<void>> tasks;
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    tasks.emplace_back(pool.add([] { sleepMs(500); }));
    EXPECT_TRUE(pool.wait_all_for(tasks, seconds(10)));
    const auto testEnd = steady_clock::now();
    const auto testDuration = testEnd - testStart;
    EXPECT_LT(testDuration, seconds(2));
}