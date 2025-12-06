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

namespace WorkerPool {
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
            using TIterator = std::list<std::shared_ptr<WorkItem>>::iterator;
            TIterator thisIterator;

        public:
            explicit WorkItem(Pool& owner,
                              size_t id, std::packaged_task<std::any()> task);

            WorkItem(const WorkItem& other) = delete;

            void enableDeletion(TIterator self);

            [[nodiscard]] bool operator==(const WorkItem& other) const;

            [[nodiscard]] bool trySetExecuting();

            void execute();

            [[nodiscard]] Pool& getOwningPool() const;

            [[nodiscard]] std::any getResult();
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
              threadFactory([threadFactory](const std::function<void()>& callback) {
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
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A Task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
            requires invocable_returns_void<TCallback, TArgs...>
        auto add(TCallback callback, TArgs... args) -> Task<void>;

    private:
        size_t lastItemId = 0;

        [[nodiscard]] bool threadIsExtra() const;

        [[nodiscard]] bool threadShouldExit() const;

        void work();

        void wait(WorkItem& workItem);

        template<typename TResult>
        static void naiveWaitAll(Task<TResult>* tasks, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                tasks[i].wait();
            }
        }

    public:
        /**
         * Blocks until all of the given tasks are finished.
         * @tparam TResult Type of the result of each task.
         * @param tasks Array of tasks.
         * @param count Number of tasks.
         */

        template<typename TResult>
        void waitAll(Task<TResult>* tasks, size_t count);

        /**
        * Blocks until all of the given tasks are finished.
         * @tparam TResult Type of the result of each task.
         * @param tasks Vector of tasks.
         */
        template<typename TResult>
        void waitAll(std::vector<Task<TResult>>& tasks) {
            waitAll(tasks.data(), tasks.size());
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
         * Gets the result returned from this task, waiting if necessary.
         * @return The result returned from this task.
         */
        [[nodiscard]] TResult getResult() {
            return wait();
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
    };

    // Overload for non-void callback
    template<typename TCallback, typename... TArgs>
    auto Pool::add(TCallback callback, TArgs... args) -> Task<decltype(std::invoke(callback, args...))> {
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
    auto Pool::add(TCallback callback, TArgs... args) -> Task<void> {
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

    template<typename TResult>
    void Pool::waitAll(Task<TResult>* tasks, size_t count) {
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
}
