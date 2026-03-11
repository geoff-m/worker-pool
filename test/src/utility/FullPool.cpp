#include "FullPool.h"

FullPool::FullPool(worker_pool::pool& pool, bool fillQueue)
    : state(State::FILLING) {
    unsigned int runningTasks = 0;
    // Add tasks to make all thread busy.
    for (unsigned int i = 1; i <= pool.get_target_parallelism(); ++i) {
        tasks.emplace_back(pool.add([&] {
            std::unique_lock lock(mutex);
            ++runningTasks;
            printf("FullQueue thread-occupying task started (runningTasks = %d)\n", runningTasks);
            fflush(stdout);
            cv.notify_all();
            cv.wait(lock, [&] {
                return state == State::EXITING;
            });
            printf("FullQueue task exiting\n");
            fflush(stdout);
        }));
    }
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return runningTasks == pool.get_target_parallelism(); });

    if (fillQueue) {
        // Add tasks to make queue full.
        for (unsigned int i = 1; i <= pool.get_max_queue_size(); ++i) {
            tasks.emplace_back(pool.add([&, i] {
                std::unique_lock lock(mutex);
                printf("FullQueue queue-occupying task %d started\n", i);
                fflush(stdout);
                cv.notify_all();
                cv.wait(lock, [&] {
                    return state == State::EXITING;
                });
                printf("FullQueue task exiting\n");
                fflush(stdout);
            }));
        }
    }
    state = State::FULL;
    printf("FullQueue believes queue to be full\n");
}

FullPool::~FullPool() {
    release();
    worker_pool::pool::wait_all(tasks);
}


void FullPool::release() {
    std::lock_guard lock(mutex);
    {
        state = State::EXITING;
    }
    cv.notify_all();
}
