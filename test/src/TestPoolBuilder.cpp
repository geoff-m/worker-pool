#include "TestUtils.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;

TEST(PoolBuilder, Nothing) {
    pool_builder b;
    auto p = b.build();
}

TEST(PoolBuilder, Name) {
    pool_builder b;
    const std::string EXPECTED_NAME = "q3y9824ewi";
    b.set_name(EXPECTED_NAME);
    EXPECT_EQ(EXPECTED_NAME, b.get_name());
    auto p = b.build();
    EXPECT_EQ(EXPECTED_NAME, p.get_name());
}

TEST(PoolBuilder, TargetParallelism) {
    pool_builder b;
    constexpr auto PARALLELISM = 561;
    b.set_target_parallelism(PARALLELISM);
    EXPECT_EQ(PARALLELISM, b.get_target_parallelism());
}

TEST(PoolBuilder, ExtraThreads) {
    pool_builder b;
    constexpr auto EXTRA_THREADS = 518;
    b.set_extra_threads(EXTRA_THREADS);
    EXPECT_EQ(EXTRA_THREADS, b.get_extra_threads().value());
}

TEST(PoolBuilder, AllowWorkOffPoolThreads) {
    pool_builder b;
    b.set_allow_work_off_pool_threads(false);
    EXPECT_FALSE(b.get_allow_work_off_pool_threads());
    b.set_allow_work_off_pool_threads(true);
    EXPECT_TRUE(b.get_allow_work_off_pool_threads());
    b.set_allow_work_off_pool_threads(false);
    EXPECT_FALSE(b.get_allow_work_off_pool_threads());
}

TEST(PoolBuilder, QueueSize) {
    pool_builder b;
    constexpr auto QUEUE_SIZE = 98765;
    b.set_queue_size(QUEUE_SIZE);
    EXPECT_EQ(QUEUE_SIZE, b.get_queue_size());
}

void testFullQueuePolicy(FullQueuePolicy fqp) {
    pool_builder b;
    b.set_full_queue_policy(fqp);
    EXPECT_EQ(fqp, b.get_full_queue_policy());
}

TEST(PoolBuilder, FullQueuePolicy) {
    testFullQueuePolicy(FullQueuePolicy::Block);
    testFullQueuePolicy(FullQueuePolicy::DropOld);
    testFullQueuePolicy(FullQueuePolicy::DropNew);
}

TEST(PoolBuilder, ThreadFactory) {
    pool_builder b;
    std::atomic<int> count = 0;
    b.set_thread_factory([&](const std::function<void()>& cb) {
        ++count;
        return std::thread(cb);
    });
    constexpr auto TARGET_THREADS = 5;
    constexpr auto EXTRA_THREADS = 7;
    b.set_target_parallelism(TARGET_THREADS);
    b.set_extra_threads(EXTRA_THREADS);
    {
        auto p = b.build();
    } // Ensure all threads have started and joined.
    EXPECT_EQ(TARGET_THREADS + EXTRA_THREADS, count);
}


TEST(PoolBuilder, BuildTwice) {
    pool_builder b;
    (void)b.build();
    EXPECT_ANY_THROW((void)b.build());
}