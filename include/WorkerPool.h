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
static thread_local WorkerPool *threadOwningPool;

static void log([[maybe_unused]] const char *format...) {
#ifdef WORKER_POOL_LOGGING
    va_list args;
    va_start(args, format);
    char buf[256];
    buf[sizeof(buf) - 1] = '\0';
    const auto timeNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    const auto threadKind = threadOwningPool != nullptr ? "pool" : "non-pool";
    const auto prefixLength = snprintf(buf, sizeof(buf), "%ld %s thread %lu: ",
                                       timeNanos, threadKind, pthread_self());
    vsnprintf(buf + prefixLength, sizeof(buf) - prefixLength, format, args);
    puts(buf);
    va_end(args);
#endif
}

class WorkerPool {
    std::mutex threadsMutex;
    std::atomic<size_t> readyThreads; // The number of threads that are doing work or are ready to do so.
    std::list<std::thread> threads;
    const size_t targetParallelism;
    const size_t maxWaiterThreads;

public:
    /**
     *
     * @param targetParallelism The target number of threads to use for simultaneous work.
     * @param maxWaiterThreads The maximum number of extra threads to create when wait is called by a pool thread.
     */
    WorkerPool(int targetParallelism, int maxWaiterThreads)
        : targetParallelism(targetParallelism),
          maxWaiterThreads(maxWaiterThreads) {
        if (targetParallelism <= 0)
            throw std::invalid_argument("Target parallelism must be at least 1");
        if (maxWaiterThreads < 0)
            throw std::invalid_argument("Maximum waiter threads must be nonnegative");
        std::lock_guard lock(threadsMutex);
        for (int i = 0; i < targetParallelism; i++)
            unsafeAddThread();
    }

    explicit WorkerPool(int targetParallelism)
        : targetParallelism(targetParallelism),
          maxWaiterThreads(targetParallelism) {
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
        {
            std::lock_guard lock(unstartedMutex);
            stopping.store(true, std::memory_order::release);
        }
        cv.notify_all();
    }

private:
    std::condition_variable cv;
    std::mutex unstartedMutex;
    class WorkItem;
    std::list<std::shared_ptr<WorkItem>> unstarted;
    std::atomic<bool> stopping = false;

    void throwIfStopped() const {
        if (stopping.load(std::memory_order::acquire))
            throw std::runtime_error("Cannot add to stopped thread pool");
    }

    void unsafeAddThread() {
        readyThreads.fetch_add(1);
        threads.emplace_back([this] { work(); });
    }

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
                          size_t id, std::packaged_task<std::any()> task)
            : id(id), owningPool(owner), task(std::move(task)), future(this->task.get_future()) {
            state.store(State::Unstarted, std::memory_order::release);
        }

        void enableDeletion(decltype(unstarted)::iterator self) {
            this->thisIterator = self;
        }

        bool operator==(const WorkItem& other) const {
            return id == other.id && &owningPool == &other.owningPool;
        }

        [[nodiscard]] bool trySetExecuting() {
            State oldState = State::Unstarted;
            if (state.compare_exchange_strong(oldState, State::Executing)) {
                log("trySetExecuting succeeded for task %p", reinterpret_cast<const void*>(this));
                return true;
            }
            log("trySetExecuting failed for task %p", reinterpret_cast<const void*>(this));
            return false;
        }

        void execute() {
            log("Beginning task %p", reinterpret_cast<const void*>(this));
            task();
            state.store(State::Done, std::memory_order::release);
            log("Finished task %p", reinterpret_cast<const void*>(this));
        }

        std::any getResult() {
            return future.get();
        }
    };

public:
    template<typename TResult>
    class Task {
        friend class WorkerPool;
        std::shared_ptr<WorkItem> wi;

        explicit Task(const std::shared_ptr<WorkItem>& wrapped) : wi(wrapped) {
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

public:
    void wait(WorkItem& workItem) {
        // retry loop
        while (true) {
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
                    log("Wait called for unstarted task %p", reinterpret_cast<const void*>(&workItem));
                    bool doExecute = false;
                    {
                        std::lock_guard lock(unstartedMutex);
                        if (workItem.trySetExecuting()) {
                            unstarted.erase(workItem.thisIterator);
                            doExecute = true;
                        }
                    }
                    if (doExecute) {
                        workItem.execute();
                        return;
                    }
                    continue; // retry wait on this item.
                }
                case WorkItem::State::Executing: {
                    // Waiting for an item that's currently being executed.
                    if (&workItem.owningPool == this && threadOwningPool == this) {
                        // We are about to block a pool thread, so consider creating an extra thread.
                        if (readyThreads.load(std::memory_order::acquire) < targetParallelism + maxWaiterThreads) {
                            // We have quota to create an extra thread to make up for waiting.
                            log("Wait called: creating extra thread");
                            std::lock_guard lock(threadsMutex);
                            unsafeAddThread();
                        }
                        log("Wait called: not creating extra thread because no quota");
                    } else {
                        log("Wait called: not creating extra thread because waiter is a non-pool thread");
                    }
                    // Block this thread.
                    workItem.future.wait();
                    return;
                }
            }
        }
    }

    [[nodiscard]] bool threadIsExtra() const {
        return readyThreads.load(std::memory_order::acquire) > targetParallelism;
    }

    [[nodiscard]] bool threadShouldExit() const {
        return stopping.load(std::memory_order::acquire) || threadIsExtra();
    }

    void work() {
        threadOwningPool = this;
        while (true) {
            std::unique_lock unstartedLock(unstartedMutex);
            cv.wait(unstartedLock, [&]() {
                return !unstarted.empty() || threadShouldExit();
            });

            if (stopping.load(std::memory_order::acquire) && unstarted.empty())
                return;

            // Take the first item that we can successfully mark as executing.
            auto item = std::find_if(unstarted.begin(), unstarted.end(),
                                     [](const std::shared_ptr<WorkItem>& wi) {
                                         return wi->trySetExecuting();
                                     });
            if (item == unstarted.end())
                continue; // Failed to mark any item as executing.
            // Copy the value because we're about to use it
            // after invalidating the iterator,
            // plus to increment the reference count.
            auto itemValue = *item;
            unstarted.erase(item);
            unstartedLock.unlock();
            itemValue->execute();
        }
    }
};
