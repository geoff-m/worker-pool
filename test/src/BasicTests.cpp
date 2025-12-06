#include "TestUtils.h"
#include "WorkerPool.h"
#include <chrono>
#include <iostream>

using namespace  WorkerPool;

TEST(BasicTests, Create0) {
    EXPECT_ANY_THROW(Pool pool(0););
}

TEST(BasicTests, Create1) {
    Pool pool(1);
}

TEST(BasicTests, Create2) {
    Pool pool(2);
}

TEST(BasicTests, IntLambda) {
    Pool pool(2);
    constexpr auto VALUE = 123;
    auto intFuture = pool.add([] { return VALUE; });
    intFuture.wait();
    EXPECT_EQ(VALUE, intFuture.getResult());
}

TEST(BasicTests, StringLambda) {
    Pool pool(2);
    constexpr auto VALUE = "hello";
    auto stringFuture = pool.add([] { return VALUE; });
    stringFuture.wait();
    EXPECT_EQ(VALUE, stringFuture.getResult());
}

int add(int x, int y) {
    return x + y;
}

TEST(BasicTests, BinaryFunction) {
    Pool pool(2);
    constexpr auto X = 2;
    constexpr auto Y = 3;
    auto binaryFuture = pool.add(add, X, Y);
    binaryFuture.wait();
    EXPECT_EQ(X + Y, binaryFuture.getResult());
}

TEST(BasicTests, BinaryLambda) {
    Pool pool(2);
    constexpr auto X = 2;
    constexpr auto Y = 3;
    auto binaryLambdaFuture = pool.add([](int x, int y) { return x + y; },
                                       X, Y);
    binaryLambdaFuture.wait();
    EXPECT_EQ(X + Y, binaryLambdaFuture.getResult());
}

TEST(BasicTests, VoidLambda) {
    Pool pool(2);
    int voidSideEffect = 0;
    constexpr auto VALUE = 123;
    auto voidFuture = pool.add([&voidSideEffect] { voidSideEffect = VALUE; });
    voidFuture.wait();
    EXPECT_EQ(VALUE, voidSideEffect);
}

TEST(BasicTests, BinaryVoidLambda) {
    Pool pool(2);
    int binaryVoidSideEffect = 0;
    constexpr auto X = 2;
    constexpr auto Y = 3;
    auto binaryVoidFuture = pool.add([&binaryVoidSideEffect](const int x, const int y) {
        binaryVoidSideEffect = x + y;
    }, X, Y);
    binaryVoidFuture.wait();
    EXPECT_EQ(X + Y, binaryVoidSideEffect);
}

void increment(int* value) {
    *value += 1;
}

TEST(BasicTests, VoidFunction) {
    Pool pool(2);
    constexpr auto INPUT = 123;
    int actual = INPUT;
    int expected = INPUT;
    increment(&expected);
    auto voidFuture = pool.add(&increment, &actual);
    voidFuture.wait();
    EXPECT_EQ(expected, actual);
}

TEST(BasicTests, AddAfterShutdown) {
    Pool pool(1);
    pool.shutDown();
    EXPECT_ANY_THROW(pool.add([]{}););
}

TEST(StressTests, Parallelism) {
    // This test tries to wait for WAIT_SECONDS seconds using threadCount threads.
    // We assert that this takes about WAIT_SECONDS in total.
    const unsigned int threadCount = std::max(1u, std::thread::hardware_concurrency());
    std::cout << "Using threadCount=" << threadCount << std::endl;
    Pool pool(threadCount);
    std::vector<Task<void>> futures;
    constexpr auto WAIT_MS = 1000;
    for (size_t i = 1; i <= threadCount; ++i) {
        futures.emplace_back(pool.add([=] { sleepMs(WAIT_MS); }));
    }
    const auto waitStartTime = std::chrono::steady_clock::now();
    for (size_t i = 0; i < threadCount; ++i)
         futures[i].wait();
    const auto waitEndTime = std::chrono::steady_clock::now();
    const auto waitDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(waitEndTime - waitStartTime).count();
    EXPECT_NEAR(WAIT_MS, waitDurationMs, 250);
}

TEST(BasicTests, RunSynchronouslyDuringWait) {
    Pool pool(1, 0, false);
    const auto startTime = std::chrono::steady_clock::now();
    pool.add([&] {
        auto sub = pool.add([] { sleepMs(1000); });
        sub.wait();
    }).wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_LT(durationMs, 1500);
}

TEST(BasicTests, WaitAllIsSmart) {
    Pool pool(2, 0, false);
    const auto startTime = std::chrono::steady_clock::now();
    pool.add([&] {
        std::vector<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([] { sleepMs(1000); }));

        // Give time for pool to start sub1.
        sleepMs(100);

        auto sub2 = subtasks.emplace_back(pool.add([] { sleepMs(1000); }));

        // This should do sub2 first instead of naively blocking on sub1.
        pool.waitAll(subtasks);
    }).wait();
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_LT(durationMs, 1500);
}

TEST(BasicTests, WaitAllIterators) {
    Pool pool(1, 0, false);
    std::atomic<int> done = 0;
    pool.add([&] {
        std::vector<Task<void>> subtasks;
        auto sub1 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub2 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub3 = subtasks.emplace_back(pool.add([&] { ++done; }));
        auto sub4 = subtasks.emplace_back(pool.add([&] { ++done; }));
        pool.waitAll(subtasks.begin(), subtasks.end());
    }).wait();
    ASSERT_EQ(4, done);
}
