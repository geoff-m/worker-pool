#include "TestUtils.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;

TEST(InvalidTask, Create) {
    task<void> v;
    task<int> i;
}

const auto ONE_SECOND = std::chrono::seconds(1);
const auto NOW = std::chrono::steady_clock::now();

TEST(InvalidTask, Wait) {
    task<void> v;
    task<int> i;
    EXPECT_ANY_THROW(v.wait());
    EXPECT_ANY_THROW(i.wait());
    EXPECT_ANY_THROW(v.wait_for(ONE_SECOND));
    EXPECT_ANY_THROW(i.wait_for(ONE_SECOND));
    EXPECT_ANY_THROW(v.wait_until(NOW));
    EXPECT_ANY_THROW(i.wait_until(NOW));
}

TEST(InvalidTask, Get) {
    task<void> v;
    task<int> i;
    EXPECT_ANY_THROW(v.get());
    EXPECT_ANY_THROW((void)i.get());
}

TEST(InvalidTask, GetName) {
    task<void> v;
    task<int> i;
    EXPECT_ANY_THROW((void)v.get_name());
    EXPECT_ANY_THROW((void)i.get_name());
}

TEST(InvalidTask, GetState) {
    task<void> v;
    task<int> i;
    EXPECT_ANY_THROW((void)v.get_state());
    EXPECT_ANY_THROW((void)i.get_state());
    EXPECT_ANY_THROW((void)v.is_unstarted());
    EXPECT_ANY_THROW((void)i.is_unstarted());
    EXPECT_ANY_THROW((void)v.is_executing());
    EXPECT_ANY_THROW((void)i.is_executing());
    EXPECT_ANY_THROW((void)v.is_done());
    EXPECT_ANY_THROW((void)i.is_done());
    EXPECT_ANY_THROW((void)v.is_canceled());
    EXPECT_ANY_THROW((void)i.is_canceled());
}

TEST(InvalidTask, TryCancel) {
    task<void> v;
    task<int> i;
    EXPECT_ANY_THROW(v.try_cancel());
    EXPECT_ANY_THROW(i.try_cancel());
}

TEST(InvalidTask, Overwrite) {
    task<void> v;
    task<int> i;
    EXPECT_ANY_THROW(v.get());
    EXPECT_ANY_THROW((void)i.get());
    pool p(1);
    v = p.add([] {});
    i = p.add([] {return 123;});
    v.get();
    EXPECT_EQ(123, i.get());
}