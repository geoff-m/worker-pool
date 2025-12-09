#include "TestUtils.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;

TEST(ResultAccess, Multiple) {
    pool pool(2);
    constexpr auto VALUE = 123;
    auto intFuture = pool.add([] { return VALUE; });
    intFuture.wait();
    EXPECT_EQ(VALUE, intFuture.get());
    EXPECT_EQ(VALUE, intFuture.get());
    EXPECT_EQ(VALUE, intFuture.get());
}

TEST(ResultAccess, GetResultBeforeWait) {
    pool pool(2);
    constexpr auto VALUE = 123;
    auto intFuture = pool.add([] { return VALUE; });
    EXPECT_EQ(VALUE, intFuture.get());
    intFuture.wait();
}

TEST(ResultAccess, MultipleGetResultBeforeWait) {
    pool pool(2);
    constexpr auto VALUE = 123;
    auto intFuture = pool.add([] { return VALUE; });
    EXPECT_EQ(VALUE, intFuture.get());
    EXPECT_EQ(VALUE, intFuture.get());
    EXPECT_EQ(VALUE, intFuture.get());
    intFuture.wait();
}