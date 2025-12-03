#include "TestUtils.h"
#include "gtest/gtest.h"
#include "WorkerPool.h"

using namespace  WorkerPool;

TEST(PreventWorkOffPoolThreads, WaitAll) {
    Pool pool(5, 0, false);
    const auto mainThreadId = std::this_thread::get_id();
    std::vector<WorkerPool::Task<void>> tasks;
    for (int i = 0; i < 10; ++i) {
        tasks.emplace_back(pool.add([mainThreadId] {
            sleepMs(500);
            ASSERT_NE(mainThreadId, std::this_thread::get_id());
        }));
    }
    pool.waitAll(tasks);
}

TEST(PreventWorkOffPoolThreads, WaitOne) {
    Pool pool(1, 0, false);
    const auto mainThreadId = std::this_thread::get_id();
    std::vector<WorkerPool::Task<void>> tasks;
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
