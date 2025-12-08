#pragma once
#include <algorithm>
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

namespace WorkerPool {
    void log([[maybe_unused]] const char* format...);

    class Pool;
    inline thread_local Pool* threadOwningPool;

    template<typename TResult>
    class Task;

    /**
     * A thread pool to which tasks can be added as callbacks.
     * The pool eventually executes all tasks ever added to it.
     * The pool tends to begin executing tasks in FIFO order, but this is not guaranteed.
     */
    class Pool {
        class WorkItem {
            friend class Pool;

            enum class State {
                Unstarted,
                Executing,
                Done
            };

            [[nodiscard]] static const char* workItemStateToString(State state);

            size_t id;
            std::atomic<State> state;
            Pool& owningPool;
            std::packaged_task<std::any()> task;
            std::shared_future<std::any> future;
            const std::string name;
            using TIterator = std::list<std::shared_ptr<WorkItem>>::iterator;
            TIterator thisIterator;

        public:
            explicit WorkItem(Pool& owner,
                              size_t id,
                              std::packaged_task<std::any()> task,
                              std::string name);

            WorkItem(const WorkItem& other) = delete;

            void enableDeletion(TIterator self);

            [[nodiscard]] bool operator==(const WorkItem& other) const;

            [[nodiscard]] bool trySetExecuting();

            void execute();

            [[nodiscard]] Pool& getOwningPool() const;

            [[nodiscard]] std::any getResult();

            [[nodiscard]] std::string getName() const;
        };

        template<typename T>
        friend class Task;
        friend class WorkItem;
        std::mutex threadsMutex;
        std::atomic<unsigned int> readyThreads; // The number of threads that are doing work or are ready to do so.
        std::list<std::thread> threads;
        const unsigned int targetParallelism;
        const unsigned int maxWaiterThreads;
        const std::function<std::thread(std::function<void()>)> threadFactory;
        const bool allowWorkOffPoolThreads;

    public:
        /**
         * Creates a new WorkerPool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param threadFactory A callable like std::thread::thread(callback) which the WorkerPool will use
         * to create threads when needed.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        template<typename ThreadFactory>
        Pool(unsigned int targetParallelism, unsigned int extraThreads, ThreadFactory threadFactory,
             bool allowWorkOffPoolThreads = true)
            : targetParallelism(targetParallelism),
              maxWaiterThreads(extraThreads),
              threadFactory([&threadFactory](const std::function<void()>& callback) {
                  return threadFactory(std::move(callback));
              }),
              allowWorkOffPoolThreads(allowWorkOffPoolThreads) {
            if (targetParallelism <= 0)
                throw std::invalid_argument("Target parallelism must be at least 1");
            std::lock_guard lock(threadsMutex);
            for (unsigned int i = 0; i < targetParallelism; i++)
                unsafeAddThread();
        }

        /**
         * Creates a new WorkerPool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        Pool(unsigned int targetParallelism, unsigned int extraThreads, bool allowWorkOffPoolThreads = true) : Pool(
            targetParallelism, extraThreads,
            [](const std::function<void()>& callback) { return std::thread(callback); },
            allowWorkOffPoolThreads) {
        }

        /**
         * Creates a new WorkerPool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
        */
        explicit Pool(unsigned int targetParallelism) : Pool(targetParallelism, targetParallelism) {
        }

        /**
         * Destroys the WorkerPool.
         * Shuts down the WorkerPool (see WorkerPool::shutDown()),
         * then waits for all work previously added to the pool to finish.
         */
        ~Pool();

        /**
         * Shuts down the WorkerPool.
         * Tasks already running or queued in the pool will still run normally.
         * Attempting to add tasks to a shut down WorkerPool will throw an exception.
         * A shut down pool cannot be restarted.
         */
        void shutDown();

    private:
        std::condition_variable cv;
        std::mutex unstartedMutex;
        std::list<std::shared_ptr<WorkItem>> unstarted;
        std::atomic<bool> stopping = false;

        void throwIfStopped() const;

        void unsafeAddThread();

    public:
        /**
         * Adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A Task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
        auto add(TCallback callback, TArgs... args) -> Task<decltype(std::invoke(callback, args...))>;

        /**
         * Adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param name Name of the task
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A Task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
        auto add(std::string name, TCallback callback, TArgs... args) -> Task<decltype(std::invoke(callback, args...))>;

        /**
         * Adds a void callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A Task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
            requires invocable_returns_void<TCallback, TArgs...>
        auto add(TCallback callback, TArgs... args) -> Task<void>;

        /**
         * Adds a void callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param name Name of the task
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A Task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
            requires invocable_returns_void<TCallback, TArgs...>
        auto add(std::string name, TCallback callback, TArgs... args) -> Task<void>;

    private:
        size_t lastItemId = 0;

        [[nodiscard]] bool threadIsExtra() const;

        [[nodiscard]] bool threadShouldExit() const;

        void work();

        void wait(WorkItem& workItem);

        void maybeAddThreadBeforeWait(const WorkItem& workItem);

        template<class Rep, class Period>
        bool wait(WorkItem& workItem, const std::chrono::duration<Rep, Period>& timeout) {
            auto state = workItem.state.load(std::memory_order::acquire);
            log("Timed wait for task %s (task is %s)",
                workItem.getName().c_str(), WorkItem::workItemStateToString(state));
            switch (state) {
                case WorkItem::State::Done:
                    return true;
                default:
                    // Waiting for an item that's currently being executed.
                    maybeAddThreadBeforeWait(workItem);
                    // Block this thread.
                    const auto status = workItem.future.wait_for(timeout);

                    log("Done with timed wait for %s (task is %s)",
                        workItem.getName().c_str(),
                        WorkItem::workItemStateToString(workItem.state.load(std::memory_order::acquire))
                    );
                    return status == std::future_status::ready;
            }
        }

        template<typename TaskIterator>
        static void naiveWaitAll(TaskIterator begin, TaskIterator end) {
            if (begin == end)
                return;
            log("naiveWaitAll(%s .. %s)", begin->getName().c_str(), std::prev(end)->getName().c_str());
            for (auto it = begin; it != end; ++it) {
                it->wait();
            }
        }

        template<typename TaskIterator, class Rep, class Period>
        static bool naiveWaitAll(TaskIterator begin, TaskIterator end, std::chrono::duration<Rep, Period> timeout) {
            if (begin == end)
                return true;
            log("naiveWaitAll(%s .. %s)", begin->getName().c_str(), std::prev(end)->getName().c_str());
            auto remainingTimeout = duration_cast<std::chrono::steady_clock::duration>(timeout);
            for (auto it = begin; it != end; ++it) {
                if (remainingTimeout < std::chrono::milliseconds(0))
                    return false;
                const auto waitStartTime = std::chrono::steady_clock::now();
                if (!it->wait(remainingTimeout))
                    return false;
                const auto waitEndTime = std::chrono::steady_clock::now();
                remainingTimeout = remainingTimeout - (waitEndTime - waitStartTime);
            }
            return true;
        }

    public:
        /**
         * Blocks until all of the given tasks are finished.
         * @tparam TResult Type of the result of each task.
         * @param tasks Array of tasks.
         * @param count Number of tasks.
         */
        template<typename TResult>
        void waitAll(Task<TResult>* tasks, size_t count) {
            waitAll(tasks, tasks + count);
        }

        /**
         * Blocks until all of the given tasks are finished.
         * @tparam TResult Type of the result of each task.
         * @param tasks Array of tasks.
         * @param count Number of tasks.
         * @param timeout The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<typename TResult, class Rep, class Period>
        bool waitAll(Task<TResult>* tasks, size_t count, std::chrono::duration<Rep, Period> timeout) {
            return waitAll(tasks, tasks + count, timeout);
        }

        /**
         * Blocks until all of the given tasks are finished.
         * @tparam TaskIterator Type of iterator for task to be awaited.
         * @param begin Iterator pointing to the first task to be awaited.
         * @param end Iterator pointing one past the last task to be awaited.
         */
        template<typename TaskIterator>
        void waitAll(TaskIterator begin, TaskIterator end);

        /**
         * Blocks until all of the given tasks are finished.
         * @tparam TaskIterator Type of iterator for task to be awaited.
         * @param begin Iterator pointing to the first task to be awaited.
         * @param end Iterator pointing one past the last task to be awaited.
         * @param timeout The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<typename TaskIterator, class Rep, class Period>
        bool waitAll(TaskIterator begin, TaskIterator end, std::chrono::duration<Rep, Period> timeout) {
            return naiveWaitAll(begin, end, timeout);
        }

        /**
         * Blocks until all of the given tasks are finished.
         * @tparam IterableTasks Type of iterable thing for tasks to be awaited.
         * @param tasks Iterable thing for tasks to be awaited.
         */
        template<typename IterableTasks>
        void waitAll(IterableTasks tasks) {
            waitAll(tasks.begin(), tasks.end());
        }

        /**
         * Blocks until all of the given tasks are finished.
         * @tparam IterableTasks Type of iterable thing for tasks to be awaited.
         * @param tasks Iterable thing for tasks to be awaited.
         * @param timeout The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<typename IterableTasks, class Rep, class Period>
        bool waitAll(IterableTasks tasks, std::chrono::duration<Rep, Period> timeout) {
            return waitAll(tasks.begin(), tasks.end(), timeout);
        }
    };

    /**
      * Represents a task that has been submitted to the pool.
      * @tparam TResult The type of the result of this Task.
      */
    template<typename TResult>
    class Task {
        friend class Pool;
        std::shared_ptr<Pool::WorkItem> wi;

        explicit Task(const std::shared_ptr<Pool::WorkItem>& wrapped)
            : wi(wrapped) {
        }

    public:
        /**
         * Blocks until this Task is complete.
         * @return The result returned from this task.
         */
        TResult wait() {
            wi->getOwningPool().wait(*wi);
            return any_cast<TResult>(wi->getResult());
        }

        /**
         * Blocks until this Task is complete, or until the given timeout elapses.
         * @param duration The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<class Rep, class Period>
        bool wait(std::chrono::duration<Rep, Period> duration) {
            return wi->getOwningPool().wait(*wi, duration);
        }

        /**
         * Gets the result returned from this task, waiting if necessary.
         * @return The result returned from this task.
         */
        [[nodiscard]] TResult getResult() {
            return wait();
        }

        [[nodiscard]] std::string getName() const {
            return wi->getName();
        }
    };

    /**
    * Represents a task that has been submitted to the pool.
    */
    template<>
    class Task<void> {
        friend class Pool;
        std::shared_ptr<Pool::WorkItem> wi;

        explicit Task(const std::shared_ptr<Pool::WorkItem>& wrapped)
            : wi(wrapped) {
        }

    public:
        /**
         * Blocks until this Task is complete.
         */
        void wait() {
            wi->getOwningPool().wait(*wi);
        }

        /**
         * Blocks until this Task is complete, or until the given timeout elapses.
         * @param duration The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<class Rep, class Period>
        bool wait(std::chrono::duration<Rep, Period> duration) {
            return wi->getOwningPool().wait(*wi, duration);
        }

        [[nodiscard]] std::string getName() const {
            return wi->getName();
        }
    };

    template<typename TCallback, typename... TArgs>
    auto Pool::add(TCallback callback, TArgs... args) -> Task<decltype(std::invoke(callback, args...))> {
        return add("", callback, args...);
    }

    template<typename TCallback, typename... TArgs>
    auto Pool::add(std::string name, TCallback callback,
                   TArgs... args) -> Task<decltype(std::invoke(callback, args...))> {
        using TResult = decltype(std::invoke(callback, args...));
        std::lock_guard lock(unstartedMutex);
        throwIfStopped();
        auto wi = std::make_shared<WorkItem>(*this,
                                             lastItemId++,
                                             std::packaged_task<std::any()>([=] {
                                                 TResult result = std::invoke(callback, args...);
                                                 return std::any(result);
                                             }), name);
        const auto it = unstarted.emplace(unstarted.end(), wi);
        wi->enableDeletion(it);
        cv.notify_one();
        return Task<TResult>(wi);
    }

    template<typename TCallback, typename... TArgs>
        requires invocable_returns_void<TCallback, TArgs...>
    auto Pool::add(TCallback callback, TArgs... args) -> Task<void> {
        return add("", callback, args...);
    }

    template<typename TCallback, typename... TArgs>
        requires invocable_returns_void<TCallback, TArgs...>
    auto Pool::add(std::string name, TCallback callback, TArgs... args) -> Task<void> {
        std::lock_guard lock(unstartedMutex);
        throwIfStopped();
        auto wi = std::make_shared<WorkItem>(*this, lastItemId++, std::packaged_task<std::any()>([=] {
            std::invoke(callback, args...);
            return std::any(0); // dummy value
        }), name);
        const auto it = unstarted.emplace(unstarted.end(), wi);
        wi->enableDeletion(it);
        cv.notify_one();
        return Task<void>(wi);
    }

    template<typename TaskIterator>
    void Pool::waitAll(TaskIterator begin, TaskIterator end) {
        if (begin == end)
            return;
        log("waitAll(%s .. %s)", begin->getName().c_str(), std::prev(end)->getName().c_str());
        if (!allowWorkOffPoolThreads && threadOwningPool != this) {
            naiveWaitAll(begin, end);
            return;
        }

        // For each given item to await,
        // do it synchronously if it's unstarted.
        TaskIterator firstExecuting = end;
        for (auto it = begin; it != end; ++it) {
            auto& item = it->wi;
            bool needRetry;
            do {
                needRetry = false;
                const auto state = item->state.load(std::memory_order::acquire);
                log("%s state is %s", it->getName().c_str(), WorkItem::workItemStateToString(state));
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
                        if (firstExecuting == end) {
                            firstExecuting = it;
                        }
                        break;
                    }
                }
            } while (needRetry);
        }
        if (firstExecuting == end) {
            // All done.
            return;
        }
        // Everything at this point should be either Executing or Done.
        naiveWaitAll(firstExecuting, end);
    }
}
