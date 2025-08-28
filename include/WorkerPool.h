#pragma once
#include <any>
#include <functional>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <concepts>
#include <memory>
#include <atomic>
#include <stdexcept>
#include <list>

template<typename TCallback, typename... TArgs>
concept invocable_returns_void = std::invocable<TCallback, TArgs...> &&
                                 requires(TCallback&& callback, TArgs&&... args)
                                 {
                                     {
                                         std::invoke(std::forward<TCallback>(callback),
                                                     std::forward<TArgs>(args)...)
                                     } -> std::same_as<void>;
                                 };

class WorkerPool {
    std::vector<std::thread> threads;

public:
    explicit WorkerPool(int maximumParallelism) {
        if (maximumParallelism <= 0)
            throw std::invalid_argument("maximumParallelism must be at least 1");
        threads.reserve(maximumParallelism);
        for (int i = 0; i < maximumParallelism; i++) {
            threads.emplace_back([&] { work(); });
        }
    }

    ~WorkerPool() {
        shutDown();
        for (auto& thread: threads) {
            thread.join();
        }
    }

    void shutDown() {
        stopping.store(true, std::memory_order::release);
        cv.notify_all();
    }

private:
    void throwIfStopped() const {
        if (stopping.load(std::memory_order::acquire))
            throw std::runtime_error("Cannot add to stopped thread pool");
    }

public:
    // Overload for non-void callback
    template<typename TCallback, typename... TArgs>
    auto add(TCallback callback, TArgs... args) -> std::future<decltype(std::invoke(callback, args...))> {
        using TResult = decltype(std::invoke(callback, args...));
        std::lock_guard lock(unstartedMutex);
        throwIfStopped();
        unstarted.emplace_back(lastItemId++, std::packaged_task<std::any()>([=] {
            TResult result = std::invoke(callback, args...);
            return std::any(result);
        }));
        auto wi = --unstarted.end();
        cv.notify_one();
        return std::async(std::launch::deferred, [wi, this] {
            auto future = wi->task.get_future();
            future.wait();
            auto value = future.get();
            std::lock_guard lock(startedMutex);
            started.erase(wi);
            return any_cast<TResult>(value);
        });
    }

    // Overload for void callback
    template<typename TCallback, typename... TArgs>
        requires invocable_returns_void<TCallback, TArgs...>
    auto add(TCallback callback, TArgs... args) -> std::future<void> {
        std::lock_guard lock(unstartedMutex);
        throwIfStopped();
        unstarted.emplace_back(lastItemId++, std::packaged_task<std::any()>([=] {
            std::invoke(callback, args...);
            return std::any(0); // dummy value
        }));
        auto wi = --unstarted.end();
        cv.notify_one();
        return std::async(std::launch::deferred, [wi, this] {
            const auto future = wi->task.get_future();
            future.wait();
            std::lock_guard lock(startedMutex);
            started.erase(wi);
        });
    }

private:
    size_t lastItemId = 0;

    struct WorkItem {
        std::packaged_task<std::any()> task;

        explicit WorkItem(size_t id, std::packaged_task<std::any()> task)
            : task(std::move(task)), id(id) {
        }

        size_t id;

        bool operator==(const WorkItem& other) const {
            return id == other.id;
        }
    };

    std::condition_variable cv;
    std::mutex unstartedMutex;
    std::list<WorkItem> unstarted;
    std::mutex startedMutex;
    std::list<WorkItem> started;

    std::atomic<bool> stopping = false;

    void work() {
        while (true) {
            std::unique_lock unstartedLock(unstartedMutex);
            cv.wait(unstartedLock, [&]() {
                return !unstarted.empty()
                       || stopping.load(std::memory_order::acquire);
            });
            if (unstarted.empty())
                return; // stopping == true and nothing to do

            const auto item = unstarted.begin(); {
                // Move item to started list.
                std::lock_guard startedLock(startedMutex);
                started.splice(started.begin(), unstarted, item);
            }
            unstartedLock.unlock();
            item->task();
        }
    }
};
