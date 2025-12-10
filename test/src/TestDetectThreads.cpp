#include "TestUtils.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;

TEST(DetectThreads, ParallelismAtLeastHardwareConcurrency) {
    const auto EXPECTED_THREADS = std::max(1u, std::thread::hardware_concurrency());
    pool pool;
    std::vector<task<void>> tasks;
    const auto startTime = std::chrono::steady_clock::now();
    for (auto i = 0u; i < EXPECTED_THREADS; i++) {
        tasks.emplace_back(pool.add(sleepMs, 1000));
    }
    pool::wait_all(tasks);
    const auto endTime = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    EXPECT_LT(durationMs, 1500);
}