#include "WorkerPool.h"

WorkerPool::WorkerPool(int maximumParallelism)
    : maximumParallelism(maximumParallelism) {
    threads.reserve(maximumParallelism);
    for (int i = 0; i < maximumParallelism; i++) {
        threads.emplace_back([&] { work(); });
    }
}

WorkerPool::~WorkerPool() {
    shutDown();
    for (auto& thread: threads) {
        thread.join();
    }
}

void WorkerPool::shutDown() {
    stopping.store(true, std::memory_order::release);
    cv.notify_all();
}

size_t WorkerPool::WorkItem::lastId = 0;