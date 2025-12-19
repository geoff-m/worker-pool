#include "TestUtils.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;

template<typename T>
void expectUnstarted(const task<T>& task) {
    EXPECT_EQ(TaskState::Unstarted, task.get_state());
    EXPECT_TRUE(task.is_unstarted());
    EXPECT_FALSE(task.is_executing());
    EXPECT_FALSE(task.is_done());
    EXPECT_FALSE(task.is_canceled());
}

template<typename T>
void expectExecuting(const task<T>& task) {
    EXPECT_EQ(TaskState::Executing, task.get_state());
    EXPECT_FALSE(task.is_unstarted());
    EXPECT_TRUE(task.is_executing());
    EXPECT_FALSE(task.is_done());
    EXPECT_FALSE(task.is_canceled());
}

template<typename T>
void expectDone(const task<T>& task) {
    EXPECT_EQ(TaskState::Done, task.get_state());
    EXPECT_FALSE(task.is_unstarted());
    EXPECT_FALSE(task.is_executing());
    EXPECT_TRUE(task.is_done());
    EXPECT_FALSE(task.is_canceled());
}

template<typename T>
void expectCanceled(const task<T>& task) {
    EXPECT_EQ(TaskState::Canceled, task.get_state());
    EXPECT_FALSE(task.is_unstarted());
    EXPECT_FALSE(task.is_executing());
    EXPECT_FALSE(task.is_done());
    EXPECT_TRUE(task.is_canceled());
}

TEST(GetState, Int) {
    pool pool(1, 0, false);
    std::mutex mutex;
    std::condition_variable cv;
    enum State {
        T1MustWait,
        T1CanExit,
        T2MustWait,
        T2CanExit,
    };

    State state = T1MustWait;
    // We use this task to stall the pool.
    auto t1 = pool.add([&] {
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return state == T1CanExit; });
            state = T2MustWait;
        }
        cv.notify_all();
    });

    bool t2Started = false;
    auto t2 = pool.add([&] {
        {
            std::unique_lock lock(mutex);
            t2Started = true;
            cv.notify_all();
            cv.wait(lock, [&] { return state == T2CanExit; });
        }
        cv.notify_all();
        return 5;
    });
    expectUnstarted(t2);

    // Tell t1 to finish, allowing t2 to start.
    {
        std::lock_guard lock(mutex);
        state = T1CanExit;
    }
    cv.notify_all();

    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return t2Started && state == T2MustWait; });
        expectExecuting(t2);
        state = T2CanExit;
    }
    cv.notify_one();

    EXPECT_EQ(5, t2.get());
    expectDone(t2);
}

TEST(GetState, Canceled) {
    pool pool(1, 0, false);
    std::mutex mutex;
    std::condition_variable cv;
    enum State {
        T1MustWait,
        T1CanExit
    };

    State state = T1MustWait;
    // We use this task to stall the pool.
    auto t1 = pool.add([&] {
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return state == T1CanExit; });
        }
    });

    auto t2 = pool.add([&] {
        // This task should get canceled.
    });
    expectUnstarted(t2);

    EXPECT_TRUE(t2.try_cancel());
    expectCanceled(t2);

    // Allow t1 to finish.
    {
        std::lock_guard lock(mutex);
        state = T1CanExit;
    }
    cv.notify_one();

    EXPECT_NO_THROW(t1.get());
    expectDone(t1);
}

TEST(GetState, RuntimeExceptionFromVoidTask) {
    pool pool;
    constexpr auto EXPECTED_STRING = "Hello";
    auto task = pool.add([] {
        throw std::runtime_error(EXPECTED_STRING);
    });
    task.wait();
    expectDone(task);
}

TEST(GetState, IntFromVoidTask) {
    pool pool;
    constexpr int EXPECTED_INT = 7865;
    auto task = pool.add([] {
        // NOLINTNEXTLINE
        throw EXPECTED_INT;
    });
    task.wait();
    EXPECT_EQ(TaskState::Done, task.get_state());
    expectDone(task);
}

TEST(GetState, Self) {
    pool pool;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<task<void>*> taskPtr;
    auto task = pool.add([&] {
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] {
                return taskPtr != nullptr;
            });
        }
        expectExecuting(*taskPtr);
    });
    {
        std::lock_guard lock(mutex);
        taskPtr = &task;
    }
    cv.notify_one();
    task.wait();
}
