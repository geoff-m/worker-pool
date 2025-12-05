#include "TestUtils.h"
#include "WorkerPool.h"

using namespace WorkerPool;

TEST(ResultAccess, Multiple) {
    Pool pool(2);
    constexpr auto VALUE = 123;
    auto intFuture = pool.add([] { return VALUE; });
    intFuture.wait();
    EXPECT_EQ(VALUE, intFuture.getResult());
    EXPECT_EQ(VALUE, intFuture.getResult());
    EXPECT_EQ(VALUE, intFuture.getResult());
}

TEST(ResultAccess, GetResultBeforeWait) {
    Pool pool(2);
    constexpr auto VALUE = 123;
    auto intFuture = pool.add([] { return VALUE; });
    EXPECT_EQ(VALUE, intFuture.getResult());
    intFuture.wait();
}

TEST(ResultAccess, MultipleGetResultBeforeWait) {
    Pool pool(2);
    constexpr auto VALUE = 123;
    auto intFuture = pool.add([] { return VALUE; });
    EXPECT_EQ(VALUE, intFuture.getResult());
    EXPECT_EQ(VALUE, intFuture.getResult());
    EXPECT_EQ(VALUE, intFuture.getResult());
    intFuture.wait();
}