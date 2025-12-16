#include "TestUtils.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;

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
    EXPECT_EQ(TaskState::Unstarted, t2.get_state());

    // Tell t1 to finish, allowing t2 to start.
    {
        std::lock_guard lock(mutex);
        state = T1CanExit;
    }
    cv.notify_all();

    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return t2Started && state == T2MustWait; });
        EXPECT_EQ(TaskState::Executing, t2.get_state());
        state = T2CanExit;
    }
    cv.notify_one();

    EXPECT_EQ(5, t2.get());
    EXPECT_EQ(TaskState::Done, t2.get_state());
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
    EXPECT_EQ(TaskState::Unstarted, t2.get_state());

    EXPECT_TRUE(t2.try_cancel());
    EXPECT_EQ(TaskState::Canceled, t2.get_state());

    // Allow t1 to finish.
    {
        std::lock_guard lock(mutex);
        state = T1CanExit;
    }
    cv.notify_one();

    EXPECT_NO_THROW(t1.get());
    EXPECT_EQ(TaskState::Done, t1.get_state());
}