#pragma once
#include <mutex>
#include <condition_variable>
#include <vector>
#include "worker-pool/worker-pool.h"

class FullPool {
    std::mutex mutex;
    std::condition_variable cv;

    enum class State {
        FILLING,
        FULL,
        EXITING
    };

    State state;
    std::vector<worker_pool::task<void>> tasks;

public:
    explicit FullPool(worker_pool::pool& pool, bool fillQueue = true);

    ~FullPool();

private:
    void release();
};