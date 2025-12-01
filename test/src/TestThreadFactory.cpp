#include "TestUtils.h"
#include "WorkerPool.h"
#include <atomic>

TEST(ThreadFactory, Basic) {
    std::atomic<int> threadsCreated = 0;
    constexpr auto EXPECTED_THREADS_CREATED = 9;
    {
        WorkerPool pool(EXPECTED_THREADS_CREATED, 0,
                        [&threadsCreated](const std::function<void()>& callback) {
                            return std::thread([&threadsCreated, callback]() {
                                ++threadsCreated;
                                callback();
                            });
                        });
    } // deleting the pool ensures all threads have gotten through their startup.
    ASSERT_EQ(EXPECTED_THREADS_CREATED, threadsCreated);
}
