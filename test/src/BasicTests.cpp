#include "gtest/gtest.h"
#include "WorkerPool.h"
#include <chrono>
#include <iostream>

TEST(BasicTests, Create0) {
    EXPECT_ANY_THROW(WorkerPool pool(0););
}

TEST(BasicTests, Create1) {
    WorkerPool pool(1);
}

TEST(BasicTests, Create2) {
    WorkerPool pool(2);
}

TEST(BasicTests, IntLambda) {
    WorkerPool pool(2);
    constexpr auto VALUE = 123;
    auto intFuture = pool.add([] { return VALUE; });
    intFuture.wait();
    EXPECT_EQ(VALUE, intFuture.get());
}

TEST(BasicTests, StringLambda) {
    WorkerPool pool(2);
    constexpr auto VALUE = "hello";
    auto stringFuture = pool.add([] { return VALUE; });
    stringFuture.wait();
    EXPECT_EQ(VALUE, stringFuture.get());
}

int add(int x, int y) {
    return x + y;
}

TEST(BasicTests, BinaryFunction) {
    WorkerPool pool(2);
    constexpr auto X = 2;
    constexpr auto Y = 3;
    auto binaryFuture = pool.add(&add, X, Y);
    binaryFuture.wait();
    EXPECT_EQ(X + Y, binaryFuture.get());
}

TEST(BasicTests, BinaryLambda) {
    WorkerPool pool(2);
    constexpr auto X = 2;
    constexpr auto Y = 3;
    auto binaryLambdaFuture = pool.add([](int x, int y) { return x + y; },
                                       X, Y);
    binaryLambdaFuture.wait();
    EXPECT_EQ(X + Y, binaryLambdaFuture.get());
}

TEST(BasicTests, VoidLambda) {
    WorkerPool pool(2);
    int voidSideEffect = 0;
    constexpr auto VALUE = 123;
    auto voidFuture = pool.add([&voidSideEffect] { voidSideEffect = VALUE; });
    voidFuture.wait();
    EXPECT_EQ(VALUE, voidSideEffect);
}

TEST(BasicTests, BinaryVoidLambda) {
    WorkerPool pool(2);
    int binaryVoidSideEffect = 0;
    constexpr auto X = 2;
    constexpr auto Y = 3;
    auto binaryVoidFuture = pool.add([&binaryVoidSideEffect](int x, int y) {
        binaryVoidSideEffect = x + y;
    }, X, Y);
    binaryVoidFuture.wait();
    EXPECT_EQ(X + Y, binaryVoidSideEffect);
}

void increment(int* value) {
    *value += 1;
}

TEST(BasicTests, VoidFunction) {
    WorkerPool pool(2);
    constexpr auto INPUT = 123;
    int actual = INPUT;
    int expected = INPUT;
    increment(&expected);
    auto voidFuture = pool.add(&increment, &actual);
    voidFuture.wait();
    EXPECT_EQ(expected, actual);
}

TEST(BasicTests, AddAfterShutdown) {
    WorkerPool pool(1);
    pool.shutDown();
    EXPECT_ANY_THROW(pool.add([]{}););
}

TEST(StressTests, Parallelism) {
    // This test tries to wait for WAIT_SECONDS seconds using threadCount threads.
    // We assert that this takes about WAIT_SECONDS in total.
    const unsigned int threadCount = std::max(1u, std::thread::hardware_concurrency());
    std::cout << "Using threadCount=" << threadCount << std::endl;
    WorkerPool pool(threadCount);
    std::vector<std::future<void>> futures;
    constexpr auto WAIT_SECONDS = 1;
    for (size_t i = 1; i <= threadCount; ++i) {
        futures.emplace_back(pool.add([=] { std::this_thread::sleep_for(std::chrono::seconds(WAIT_SECONDS)); }));
    }
    const auto waitStartTime = std::chrono::steady_clock::now();
    for (size_t i = 0; i < threadCount; ++i)
         futures[i].wait();
    const auto waitEndTime = std::chrono::steady_clock::now();
    const auto waitDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(waitEndTime - waitStartTime).count();
    EXPECT_NEAR(WAIT_SECONDS * 1000, waitDurationMs, 250);
}
