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
#include <cstdio>
#include <list>

template<typename TCallback, typename TResult, typename... TArgs>
concept invocable_returns = std::invocable<TCallback, TArgs...> &&
                            requires(TCallback&& callback, TArgs&&... args)
                            {
                                {
                                    std::invoke(std::forward<TCallback>(callback),
                                                std::forward<TArgs>(args)...)
                                } -> std::convertible_to<TResult>;
                            };

template<typename TCallback, typename TResult>
concept nullary_invocable_returns = std::invocable<TCallback> && requires(TCallback&& callback)
{
    { std::invoke(std::forward<TCallback>(callback)) } -> std::convertible_to<TResult>;
};

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
public:
    explicit WorkerPool(int maximumParallelism);

    ~WorkerPool();

    void shutDown();

    // Overload for non-nullary non-void callback
    template<typename TCallback, typename... TArgs>
    auto add(TCallback callback, TArgs... args) -> std::future<decltype(std::invoke(callback, args...))> {
        using TResult = decltype(std::invoke(callback, args...));
        std::unique_lock lock(mutex);
        if (stopping.load(std::memory_order::acquire))
            throw std::runtime_error("Cannot add to stopped thread pool");
        items.emplace_back(std::packaged_task<std::any()>([=] {
            TResult result = std::invoke(callback, args...);
            return std::any(result);
        }));
        auto wi = --items.end();
        ++incompleteItems;
        cv.notify_all();
        return std::async(std::launch::deferred, [wi, this] {
            auto future = wi->task.get_future();
            future.wait();
            auto value = future.get();
            std::unique_lock lock(mutex);
            items.erase(wi);
            return any_cast<TResult>(value);
        });
    }

    // Overload for nullary non-void callbacks
    template<typename TCallback>
        requires nullary_invocable_returns<TCallback, decltype(std::invoke(TCallback()))>
    auto add(TCallback callback) -> std::future<decltype(std::invoke(callback))> {
        using TResult = decltype(std::invoke(callback));
        std::unique_lock lock(mutex);
        if (stopping.load(std::memory_order::acquire))
            throw std::runtime_error("Cannot add to stopped thread pool");
        items.emplace_back(std::packaged_task<std::any()>([=] {
            TResult result = std::invoke(callback);
            return std::any(result);
        }));
        auto wi = --items.end();
        ++incompleteItems;
        cv.notify_all();
        return std::async(std::launch::deferred, [wi, this] {
            auto future = wi->task.get_future();
            future.wait();
            auto value = future.get();
            std::unique_lock lock(mutex);
            items.erase(wi);
            return any_cast<decltype(std::invoke(callback))>(value);
        });
    }

    // Overload for non-nullary void callbacks
    template<typename TCallback, typename... TArgs>
        requires invocable_returns_void<TCallback, TArgs...>
    auto add(TCallback callback, TArgs... args) -> std::future<void> {
        std::unique_lock lock(mutex);
        if (stopping.load(std::memory_order::acquire))
            throw std::runtime_error("Cannot add to stopped thread pool");
        items.emplace_back(std::packaged_task<std::any()>([=] {
            std::invoke(callback, args...);
            return std::any(0); // dummy value
        }));
        auto wi = --items.end();
        ++incompleteItems;
        cv.notify_all();
        return std::async(std::launch::deferred, [wi, this] {
            auto future = wi->task.get_future();
            future.wait();
            std::unique_lock lock(mutex);
            items.erase(wi);
        });
    }

private:
    int maximumParallelism;

    struct WorkItem {
        std::packaged_task<std::any()> task;
        bool started = false;

        explicit WorkItem(std::packaged_task<std::any()> task)
            : task(std::move(task)), id(lastId++) {
        }

        static size_t lastId;
        size_t id;

        bool operator==(const WorkItem& other) const {
            return id == other.id;
        }
    };

    std::condition_variable cv;
    std::mutex mutex;
    std::list<WorkItem> items;
    std::atomic<size_t> incompleteItems = 0;

    std::vector<std::thread> threads;
    std::atomic<bool> stopping = false;

    void work() {
        while (true) {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&]() {
                return incompleteItems.load(std::memory_order::acquire) > 0
                       || stopping.load(std::memory_order::acquire);
            });
            if (incompleteItems.load(std::memory_order::acquire) == 0)
                return; // stopping == true and nothing to do

            // Find first incomplete item.
            for (auto& item: items) {
                if (item.started)
                    continue;

                item.started = true;
                lock.unlock();
                --incompleteItems;
                cv.notify_all();
                item.task();
                break;
            }
        }
    }
};
