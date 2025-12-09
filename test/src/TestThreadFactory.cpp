#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include <atomic>

using namespace worker_pool;

TEST(ThreadFactory, Basic) {
    std::atomic<int> threadsCreated = 0;
    constexpr auto EXPECTED_THREADS_CREATED = 9;
    {
        pool pool(EXPECTED_THREADS_CREATED, 0,
                  [&threadsCreated](const std::function<void()>& callback) {
                      return std::thread([&threadsCreated, callback]() {
                          ++threadsCreated;
                          callback();
                      });
                  });
    } // deleting the pool ensures all threads have gotten through their startup.
    ASSERT_EQ(EXPECTED_THREADS_CREATED, threadsCreated);
}
