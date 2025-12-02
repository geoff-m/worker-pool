#pragma once
#include <any>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <concepts>
#include <memory>
#include <atomic>
#include <list>
#include <utility>

#ifdef WORKER_POOL_LOGGING
#include <cstdarg>
#include <cstdio>
#endif

template<typename TCallback, typename... TArgs>
concept invocable_returns_void = std::invocable<TCallback, TArgs...> &&
                                 requires(TCallback&& callback, TArgs&&... args)
                                 {
                                     {
                                         std::invoke(std::forward<TCallback>(callback),
                                                     std::forward<TArgs>(args)...)
                                     } -> std::same_as<void>;
                                 };
class WorkerPool;
inline thread_local WorkerPool* threadOwningPool;

class WorkerPool {
    std::mutex threadsMutex;
    std::atomic<size_t> readyThreads; // The number of threads that are doing work or are ready to do so.
    std::list<std::thread> threads;
    const size_t targetParallelism;
    const size_t maxWaiterThreads;
    const std::function<std::thread(std::function<void()>)> threadFactory;
    const bool allowWorkOffPoolThreads;

public:
    /**
 *
 * @param targetParallelism The target number of threads to use for simultaneous work.
 * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
 * @param threadFactory A callable like std::thread(callback)
 * @param allowWorkOffPoolThreads Whether this pool is allowed to execute callbacks in non-pool waiter threads.
 */
    template<typename ThreadFactory>
    WorkerPool(int targetParallelism, int extraThreads, ThreadFactory threadFactory,
               bool allowWorkOffPoolThreads = true)
        : targetParallelism(targetParallelism),
          maxWaiterThreads(extraThreads),
          threadFactory([threadFactory](const std::function<void()>& callback) {
              return threadFactory(std::move(callback));
          }),
          allowWorkOffPoolThreads(allowWorkOffPoolThreads) {
        if (targetParallelism <= 0)
            throw std::invalid_argument("Target parallelism must be at least 1");
        if (extraThreads < 0)
            throw std::invalid_argument("Maximum waiter threads must be nonnegative");
        std::lock_guard lock(threadsMutex);
        for (int i = 0; i < targetParallelism; i++)
            unsafeAddThread();
    }

    WorkerPool(int targetParallelism, int maxWaiterThreads, bool allowWorkOffPoolThreads = true) : WorkerPool(
        targetParallelism, maxWaiterThreads,
        [](const std::function<void()>& callback) { return std::thread(callback); },
        allowWorkOffPoolThreads) {
    }

    explicit WorkerPool(int targetParallelism) : WorkerPool(targetParallelism, targetParallelism) {
    }

    ~WorkerPool();

    void shutDown();

private:
    std::condition_variable cv;
    std::mutex unstartedMutex;
    class WorkItem;
    std::list<std::shared_ptr<WorkItem>> unstarted;
    std::atomic<bool> stopping = false;

    void throwIfStopped() const;

    void unsafeAddThread();

    class WorkItem {
        friend class WorkerPool;

        enum class State {
            Unstarted,
            Executing,
            Done
        };

        size_t id;
        std::atomic<State> state;
        WorkerPool& owningPool;
        std::packaged_task<std::any()> task;
        std::future<std::any> future;
        decltype(unstarted)::iterator thisIterator;

    public:
        explicit WorkItem(WorkerPool& owner,
                          size_t id, std::packaged_task<std::any()> task);

        void enableDeletion(decltype(unstarted)::iterator self);

        bool operator==(const WorkItem& other) const;

        [[nodiscard]] bool trySetExecuting();

        void execute();

        std::any getResult();
    };

public:
    template<typename TResult>
    class Task {
        friend class WorkerPool;
        std::shared_ptr<WorkItem> wi;

        explicit Task(const std::shared_ptr<WorkItem>& wrapped)
            : wi(wrapped) {
        }

    public:
        void wait() {
            wi->owningPool.wait(*wi);
        }

        TResult getResult() {
            wait();
            return any_cast<TResult>(wi->future.get());
        }
    };

    // Overload for non-void callback
    template<typename TCallback, typename... TArgs>
    auto add(TCallback callback, TArgs... args) -> Task<decltype(std::invoke(callback, args...))> {
        using TResult = decltype(std::invoke(callback, args...));
        std::lock_guard lock(unstartedMutex);
        throwIfStopped();
        auto wi = std::make_shared<WorkItem>(*this, lastItemId++, std::packaged_task<std::any()>([=] {
            TResult result = std::invoke(callback, args...);
            return std::any(result);
        }));
        const auto it = unstarted.emplace(unstarted.end(), wi);
        wi->enableDeletion(it);
        cv.notify_one();
        return Task<TResult>(wi);
    }

    // Overload for void callback
    template<typename TCallback, typename... TArgs>
        requires invocable_returns_void<TCallback, TArgs...>
    auto add(TCallback callback, TArgs... args) -> Task<void> {
        std::lock_guard lock(unstartedMutex);
        throwIfStopped();
        auto wi = std::make_shared<WorkItem>(*this, lastItemId++, std::packaged_task<std::any()>([=] {
            std::invoke(callback, args...);
            return std::any(0); // dummy value
        }));
        const auto it = unstarted.emplace(unstarted.end(), wi);
        wi->enableDeletion(it);
        cv.notify_one();
        return Task<void>(wi);
    }

private:
    size_t lastItemId = 0;

    void wait(WorkItem& workItem);

    template<typename TResult>
    static void naiveWaitAll(Task<TResult>* tasks, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            tasks[i].wait();
        }
    }

public:
    template<typename TResult>
    void waitAll(Task<TResult>* tasks, size_t count) {
        if (!allowWorkOffPoolThreads && threadOwningPool != this) {
            naiveWaitAll(tasks, count);
            return;
        }

        // The index of the first item that is currently being executed.
        // Equal to count if none found yet.
        size_t firstExecutingIndex = count;

        // For each given item to await,
        // do it synchronously if it's unstarted.
        for (size_t i = 0; i < count; ++i) {
            auto& item = tasks[i].wi;
            bool needRetry = false;
            do {
                const auto state = item->state.load(std::memory_order::acquire);
                switch (state) {
                    default:
                    case WorkItem::State::Done:
                        break;
                    case WorkItem::State::Unstarted: {
                        if (item->trySetExecuting()) {
                            // This item was unstarted and now we can execute it synchronously.
                            auto itemValue = item;
                            {
                                std::lock_guard lock(unstartedMutex);
                                unstarted.erase(item->thisIterator);
                            }
                            itemValue->execute();
                        } else {
                            // Failed to start executing it.
                            // Recheck this item.
                            // Its new state may be Executing or Done.
                            needRetry = true;
                        }
                        break;
                    }
                    case WorkItem::State::Executing: {
                        if (firstExecutingIndex > i) {
                            firstExecutingIndex = i;
                        }
                        break;
                    }
                }
            } while (needRetry);
        }
        if (firstExecutingIndex == count) {
            // All done.
            return;
        }
        // Everything at this point should be either Executing or Done.
        naiveWaitAll(tasks + firstExecutingIndex, count);
    }

    template<typename TResult>
    void waitAll(std::vector<Task<TResult>>& tasks) {
        waitAll(tasks.data(), tasks.size());
    }

    [[nodiscard]] bool threadIsExtra() const;

    [[nodiscard]] bool threadShouldExit() const;

    void work();
};
