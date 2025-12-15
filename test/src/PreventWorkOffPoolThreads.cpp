#include "TestUtils.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;

TEST(PreventWorkOffPoolThreads, WaitAll) {
    pool pool(5, 0, false);
    const auto mainThreadId = std::this_thread::get_id();
    std::vector<task<void>> tasks;
    for (int i = 0; i < 10; ++i) {
        tasks.emplace_back(pool.add([mainThreadId] {
            sleepMs(500);
            ASSERT_NE(mainThreadId, std::this_thread::get_id());
        }));
    }
    pool::wait_all(tasks);
}

TEST(PreventWorkOffPoolThreads, WaitOne) {
    pool pool(1, 0, false);
    const auto mainThreadId = std::this_thread::get_id();
    std::vector<task<void>> tasks;
    for (int i = 0; i < 2; ++i) {
        tasks.emplace_back(pool.add([mainThreadId] {
            sleepMs(500);
            ASSERT_NE(mainThreadId, std::this_thread::get_id());
        }));
    }
    for (auto& task: tasks) {
        task.wait();
    }
}
