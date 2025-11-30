#include "TestUtils.h"
#include "WorkerPool.h"

TEST(ThreadFactory, Basic) {
    int threadsCreated = 0;
    constexpr auto EXPECTED_THREADS_CREATED = 9;
    WorkerPool pool(EXPECTED_THREADS_CREATED, 0,
                    [&](const std::function<void()>& callback) {
                        ++threadsCreated;
                        return std::thread(callback);
                    });
    ASSERT_EQ(EXPECTED_THREADS_CREATED, threadsCreated);
}
