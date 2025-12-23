#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include <atomic>

using namespace worker_pool;

TEST(ThreadFactory, Lambda) {
    std::atomic<int> threadsCreated = 0;
    constexpr auto EXPECTED_THREADS_CREATED = 3;
    {
        pool pool(EXPECTED_THREADS_CREATED, 0,
                  [&threadsCreated](const std::function<void()>& callback) {
                      return std::thread([&threadsCreated, callback] {
                          ++threadsCreated;
                          callback();
                      });
                  });
    } // deleting the pool ensures all threads have gotten through their startup.
    ASSERT_EQ(EXPECTED_THREADS_CREATED, threadsCreated);
}

TEST(ThreadFactory, Variable) {
    std::atomic<int> threadsCreated = 0;
    constexpr auto EXPECTED_THREADS_CREATED = 3;
    {
        auto factory = [&threadsCreated](const std::function<void()>& callback) {
            return std::thread([&threadsCreated, callback] {
                ++threadsCreated;
                callback();
            });
        };
        pool pool(EXPECTED_THREADS_CREATED, 0, factory);
    } // deleting the pool ensures all threads have gotten through their startup.
    ASSERT_EQ(EXPECTED_THREADS_CREATED, threadsCreated);
}

struct UnmovableUncopiableFactory {
    std::atomic<int> threadsCreated = 0;

    [[nodiscard]] std::thread operator()(const std::function<void()>& callback) {
        return std::thread([&, callback] {
            ++threadsCreated;
            callback();
        });
    }
};

static_assert(!std::copyable<UnmovableUncopiableFactory>);
static_assert(!std::movable<UnmovableUncopiableFactory>);

TEST(ThreadFactory, UnmovableUncopyable) {
    constexpr auto EXPECTED_THREADS_CREATED = 3;
    UnmovableUncopiableFactory factory;
    {
        pool pool(EXPECTED_THREADS_CREATED, 0, factory);
    } // deleting the pool ensures all threads have gotten through their startup.
    ASSERT_EQ(EXPECTED_THREADS_CREATED, factory.threadsCreated);
}

TEST(ThreadFactory, ConstVariable) {
    std::atomic<int> threadsCreated = 0;
    constexpr auto EXPECTED_THREADS_CREATED = 3;
    {
        const auto factory = [&threadsCreated](const std::function<void()>& callback) {
            return std::thread([&threadsCreated, callback] {
                ++threadsCreated;
                callback();
            });
        };
        pool pool(EXPECTED_THREADS_CREATED, 0, factory);
    } // deleting the pool ensures all threads have gotten through their startup.
    ASSERT_EQ(EXPECTED_THREADS_CREATED, threadsCreated);
}

struct ConstUnmovableUncopiableFactory {
    std::atomic<int>& threadsCreated;

    explicit ConstUnmovableUncopiableFactory(std::atomic<int>& threadsCreated)
        : threadsCreated(threadsCreated) {
    }

    [[nodiscard]] std::thread operator()(const std::function<void()>& callback) const {
        return std::thread([&, callback] {
            ++threadsCreated;
            callback();
        });
    }
};

static_assert(!std::copyable<ConstUnmovableUncopiableFactory>);
static_assert(!std::movable<ConstUnmovableUncopiableFactory>);

TEST(ThreadFactory, ConstUnmovableUncopyable) {
    constexpr auto EXPECTED_THREADS_CREATED = 3;
    std::atomic<int> threadsCreated = 0;
    {
        const ConstUnmovableUncopiableFactory factory(threadsCreated);
        pool pool(EXPECTED_THREADS_CREATED, 0, factory);
    } // deleting the pool ensures all threads have gotten through their startup.
    ASSERT_EQ(EXPECTED_THREADS_CREATED, threadsCreated);
}
