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