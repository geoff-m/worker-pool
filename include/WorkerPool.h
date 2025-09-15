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
    std::mutex threadsMutex;
    std::atomic<size_t> readyThreads; // The number of threads that are doing work or are ready to do so.
    std::list<std::thread> threads;
    const size_t targetParallelism;

public:
    explicit WorkerPool(int targetParallelism)
        : targetParallelism(targetParallelism) {
        if (targetParallelism <= 0)
            throw std::invalid_argument("Target parallelism must be at least 1");
        std::lock_guard lock(threadsMutex);
        for (int i = 0; i < targetParallelism; i++)
            unsafeAddThread();
    }

    ~WorkerPool() {
        shutDown();
        std::lock_guard lock(threadsMutex);
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

    void unsafeAddThread() {
        readyThreads.fetch_add(1);
        threads.emplace_back();
        auto end = std::prev(threads.end());
        new(&*end) std::thread([this, end] { work(end); });
    }

    void unsafeDeleteThread(const std::list<std::thread>::iterator thread) {
        readyThreads.fetch_sub(1);
        threads.erase(thread);
    }

public:
    // Overload for non-void callback
    template<typename TCallback, typename... TArgs>
    auto add(TCallback callback, TArgs... args) -> std::future<decltype(std::invoke(callback, args...))> {
        using TResult = decltype(std::invoke(callback, args...));
        std::lock_guard lock(unstartedMutex);
        throwIfStopped();
        unstarted.emplace_back(*this, lastItemId++, std::packaged_task<std::any()>([=] {
            TResult result = std::invoke(callback, args...);
            return std::any(result);
        }));
        auto wi = std::prev(unstarted.end());
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
        unstarted.emplace_back(*this, lastItemId++, std::packaged_task<std::any()>([=] {
            std::invoke(callback, args...);
            return std::any(0); // dummy value
        }));
        auto wi = std::prev(unstarted.end());
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

    class WorkItem {
        friend class WorkerPool;

        enum class State {
            Unstarted,
            Executing,
            Done
        };

        std::atomic<State> state;
        WorkerPool& owningPool;
        std::packaged_task<std::any()> task;
        std::future<std::any> future;

        size_t id;

    public:
        explicit WorkItem(WorkerPool& owner, size_t id, std::packaged_task<std::any()> task)
            : owningPool(owner), task(std::move(task)), id(id) {
            state.store(State::Unstarted, std::memory_order::release);
        }

        bool operator==(const WorkItem& other) const {
            return id == other.id && &owningPool == &other.owningPool;
        }

        void execute() {
            future = task.get_future();
            task();
            state.store(State::Done, std::memory_order::release);
        }

        std::any getResult() {
            return future.get();
        }
    };

    void wait(WorkItem& workItem) {
        auto state = workItem.state.load(std::memory_order::acquire);
        switch (state) {
            default:
            case WorkItem::State::Done:
                // Waiting for an item that's already done.
                // Return immediately.
                return;
            case WorkItem::State::Unstarted: {
                // Waiting for an item that hasn't begun to be executed yet.
                // Execute it synchronously.
                WorkItem::State oldState = WorkItem::State::Unstarted;
                if (!workItem.state.compare_exchange_strong(oldState, WorkItem::State::Executing))
                    return wait(workItem); // Failed to change state to executing. Retry this operation.
                workItem.execute();
                return;
            }
            case WorkItem::State::Executing: {
                // Waiting for an item that's currently being executed.
                {
                    // Create a new pool thread so that worker capacity is not reduced.
                    std::lock_guard lock(threadsMutex);
                    unsafeAddThread();
                }
                // Block this thread
                workItem.future.wait();
                break;
            }
        }
    }

    std::condition_variable cv;
    std::mutex unstartedMutex;
    std::list<WorkItem> unstarted;
    std::mutex startedMutex;
    std::list<WorkItem> started;

    std::atomic<bool> stopping = false;

    [[nodiscard]] bool threadIsExtra() const {
        return readyThreads.load(std::memory_order::acquire) > targetParallelism;
    }

    [[nodiscard]] bool threadShouldExit() const {
        return stopping.load(std::memory_order::acquire) || threadIsExtra();
    }

    void work(decltype(threads)::iterator thisThread) {
        while (true) {
            std::unique_lock unstartedLock(unstartedMutex);
            cv.wait(unstartedLock, [&]() {
                return !unstarted.empty() || threadShouldExit();
            });

            if (stopping.load(std::memory_order::acquire) && unstarted.empty())
                return; // No need to delete our thread list entry.
            if (threadIsExtra()) {
                std::unique_lock threadLock(threadsMutex, std::defer_lock);
                while (!threadLock.try_lock()) {
                    if (stopping.load(std::memory_order::acquire)) {
                        // We don't hold the thread lock, but we're stopping.
                        // Just exit. No need to delete our thread list entry.
                        return;
                    }
                }
                // We hold the thread lock.
                if (!stopping.load(std::memory_order::acquire)) {
                    unsafeDeleteThread(thisThread);
                }
                return;
            }

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
