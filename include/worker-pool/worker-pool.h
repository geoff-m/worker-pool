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
#include <vector>
#include <list>
#include <optional>
#include <utility>

#ifdef WORKER_POOL_LOGGING
#include <cstdarg>
#include <cstdio>
#endif

template<typename T>
concept is_thread_factory = requires(T factory)
{
    {
        factory([] {
        }).join()
    } -> std::same_as<void>;
};

namespace worker_pool {
    void log([[maybe_unused]] const char* format...);

    class deadlock_exception : public std::runtime_error {
    public:
        explicit deadlock_exception(const std::string& message)
            : runtime_error(message) {
        }
    };

    class canceled_exception : public std::runtime_error {
    public:
        explicit canceled_exception()
            : runtime_error("Task has been canceled") {
        }
    };

    class pool;
    inline thread_local pool* threadOwningPool;

    template<typename TResult>
    class task;

    enum class TaskState {
        // The task is queued in the pool but has not begun executing.
        Unstarted,

        // The task is executing.
        Executing,

        // The task has finished executing (either by returning or throwing).
        Done,

        // The task has been canceled while unstarted.
        Canceled
    };

    enum class FullQueuePolicy {
        // Specifies that when the pool's task queue is full,
        // attempting to add a new task should block until space becomes available for the new task.
        Block,

        // Specifies that when the pool's task queue is full,
        // attempting to add a new task should cancel a task at or near the beginning of the queue.
        DropOld,

        // Specifies that when the pool's task queue is full,
        // attempting to add a new task should immediately cancel the new task instead of adding it to the queue.
        DropNew
    };

    /**
     * A thread pool to which tasks can be added as callbacks.
     * The pool eventually executes all tasks ever added to it.
     * The pool tends to begin executing tasks in FIFO order, but this is not guaranteed.
     */
    class pool {
        class WorkItem;
        friend class WorkItem;
        std::list<std::shared_ptr<WorkItem>> unstarted;
        const size_t maxUnstarted;
        const FullQueuePolicy fullQueuePolicy;

        template<typename T>
        friend class task;
        friend class task_base;

        const std::string name;
        static std::atomic<unsigned int> id;
        std::atomic<size_t> addedTaskCount = 0;
        std::mutex threadsMutex;
        std::condition_variable threadsCv;
        std::vector<std::thread> threads;
        const unsigned int targetParallelism;
        const bool allowWorkOffPoolThreads;

        [[nodiscard]] static std::string generatePoolName();

        [[nodiscard]] std::string generateTaskName();

        class WorkItem {
            friend class pool;

            [[nodiscard]] static const char* workItemStateToString(TaskState state);

            size_t id;
            std::atomic<TaskState> state;
            pool& owningPool;
            std::packaged_task<std::any()> task;
            std::shared_future<std::any> future;
            const std::string name;
            using TIterator = decltype(unstarted)::iterator;
            TIterator thisIterator;

        public:
            explicit WorkItem(pool& owner,
                              size_t id,
                              std::string name);

            WorkItem(const WorkItem& other) = delete;

            void enableDeletion(TIterator self);

            void setCallback(std::packaged_task<std::any()>&& callback);

            void throwIfCanceled();

            [[nodiscard]] bool operator==(const WorkItem& other) const;

            [[nodiscard]] bool trySetExecuting();

            bool trySetCanceled();

            void execute();

            [[nodiscard]] pool& getOwningPool() const;

            [[nodiscard]] std::any getResult();

            [[nodiscard]] std::string getName() const;

            [[nodiscard]] TIterator getIterator() const;

            [[nodiscard]] TaskState getState() const;

#if defined(WORKER_POOL_DEADLOCK_DETECTION)
            WorkItem* waitingFor = nullptr;
#endif
        };

        std::atomic<bool> stopping = false;

        void throwIfStopped() const;

        [[nodiscard]] static unsigned int detectParallelism();

        size_t lastItemId = 0;

        // Number of threads that are either actively executing a task or are about to be.
        // If a thread is not ready, it is either waiting for more work to be enqueued, or stuck waiting for a separate thread.
        std::atomic<int> readyThreads = 0;

        void work();

        void wait(std::shared_ptr<WorkItem> workItem);

        static thread_local WorkItem* executingWorkItem;

#ifdef WORKER_POOL_DEADLOCK_DETECTION
        static void checkDeadlock(WorkItem& toAwait);

        [[nodiscard]] static std::string formatWaitChain(const WorkItem& wi);

#define FAIL_IF_WAITING_WILL_DEADLOCK(toAwait) checkDeadlock(toAwait)
#else
#define FAIL_IF_WAITING_WILL_DEADLOCK(toAwait)
#endif

    public:
        static std::function<std::thread(const std::function<void()>&)> defaultThreadFactory;

        /**
         * Creates a new pool.
         * @param name The name for this pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param queueSize The size of the task queue. When the queue is full, enqueuing a new task will block.
         * @param fullQueuePolicy The way the pool should behave when attempting to add a task in a possibly-blocking way.
         * @param threadFactory A callable like std::thread::thread(callback) which the pool will use
         * to create threads.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        template<typename ThreadFactory>
            requires is_thread_factory<ThreadFactory>
        pool(std::string name, unsigned int targetParallelism, unsigned int extraThreads, size_t queueSize,
             FullQueuePolicy fullQueuePolicy,
             ThreadFactory&& threadFactory,
             bool allowWorkOffPoolThreads = true)
            : maxUnstarted(queueSize),
              fullQueuePolicy(fullQueuePolicy),
              name(name.empty() ? generatePoolName() : std::move(name)),
              targetParallelism(targetParallelism > 0 ? targetParallelism : detectParallelism()),
              allowWorkOffPoolThreads(allowWorkOffPoolThreads) {
            std::lock_guard lock(threadsMutex);
            const auto totalThreads = targetParallelism + extraThreads;
            threads.reserve(totalThreads);
            for (unsigned int i = 0; i < totalThreads; i++) {
                threads.emplace_back(threadFactory([this] { work(); }));
            }
        }

        /**
         * Creates a new pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param queueSize The size of the task queue. When the queue is full, enqueuing a new task will block.
         * @param fullQueuePolicy The way the pool should behave when attempting to add a task in a possibly-blocking way.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param threadFactory A callable like std::thread::thread(callback) which the pool will use
         * to create threads.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        template<typename ThreadFactory>
            requires is_thread_factory<ThreadFactory>
        pool(unsigned int targetParallelism, size_t queueSize, FullQueuePolicy fullQueuePolicy,
             unsigned int extraThreads,
             ThreadFactory&& threadFactory,
             bool allowWorkOffPoolThreads = true)
            : pool(std::string(), targetParallelism, queueSize, fullQueuePolicy, extraThreads, threadFactory,
                   allowWorkOffPoolThreads) {
        }

        /**
         * Creates a new pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param queueSize The size of the task queue. When the queue is full, enqueuing a new task will block. 0 for unbounded.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param threadFactory A callable like std::thread::thread(callback) which the pool will use
         * to create threads.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        template<typename ThreadFactory>
            requires is_thread_factory<ThreadFactory>
        pool(unsigned int targetParallelism, size_t queueSize, unsigned int extraThreads,
             ThreadFactory&& threadFactory,
             bool allowWorkOffPoolThreads = true)
            : pool(std::string(), targetParallelism, queueSize, FullQueuePolicy::Block, extraThreads, threadFactory,
                   allowWorkOffPoolThreads) {
        }

        /**
         * Creates a new pool.
         * @param name The name for this pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param queueSize The size of the task queue. When the queue is full, enqueuing a new task will block. 0 for unbounded.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param threadFactory A callable like std::thread::thread(callback) which the pool will use
         * to create threads.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        template<typename ThreadFactory>
            requires is_thread_factory<ThreadFactory>
        pool(std::string name, unsigned int targetParallelism, size_t queueSize, unsigned int extraThreads,
             ThreadFactory&& threadFactory,
             bool allowWorkOffPoolThreads = true)
            : pool(std::move(name), targetParallelism, extraThreads, queueSize, FullQueuePolicy::Block, threadFactory,
                   allowWorkOffPoolThreads) {
        }

        /**
         * Creates a new pool.
         * @param name The name for this pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param threadFactory A callable like std::thread::thread(callback) which the pool will use
         * to create threads.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        template<typename ThreadFactory>
            requires is_thread_factory<ThreadFactory>
        pool(std::string name, unsigned int targetParallelism, unsigned int extraThreads,
             ThreadFactory&& threadFactory,
             bool allowWorkOffPoolThreads = true)
            : pool(std::move(name),
                   targetParallelism,
                   0,
                   extraThreads,
                   threadFactory,
                   allowWorkOffPoolThreads) {
        }

        /**
         * Creates a new pool.
         * @param name The name for this pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param threadFactory A callable like std::thread::thread(callback) which the pool will use
         * to create threads.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        template<typename ThreadFactory>
            requires is_thread_factory<ThreadFactory>
        pool(std::string name, unsigned int targetParallelism, unsigned int extraThreads,
             const ThreadFactory& threadFactory,
             bool allowWorkOffPoolThreads = true)
            : pool(name, targetParallelism, extraThreads, std::move(threadFactory), allowWorkOffPoolThreads) {
        }

        /**
         * Creates a new pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param threadFactory A callable like std::thread::thread(callback) which the pool will use
         * to create threads.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        template<typename ThreadFactory>
            requires is_thread_factory<ThreadFactory>
        pool(unsigned int targetParallelism, unsigned int extraThreads, ThreadFactory&& threadFactory,
             bool allowWorkOffPoolThreads = true)
            : pool("", targetParallelism, extraThreads, threadFactory, allowWorkOffPoolThreads) {
        }

        /**
         * Creates a new pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param threadFactory A callable like std::thread::thread(callback) which the pool will use
         * to create threads.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        template<typename ThreadFactory>
            requires is_thread_factory<ThreadFactory>
        pool(unsigned int targetParallelism, unsigned int extraThreads, const ThreadFactory& threadFactory,
             bool allowWorkOffPoolThreads = true)
            : pool("", targetParallelism, extraThreads, std::move(threadFactory), allowWorkOffPoolThreads) {
        }

        /**
         * Creates a new pool.
         * @param name The name for this pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        pool(const std::string& name, unsigned int targetParallelism, unsigned int extraThreads,
             bool allowWorkOffPoolThreads = true) : pool(
            name, targetParallelism, extraThreads,
            defaultThreadFactory,
            allowWorkOffPoolThreads) {
        }

        /**
         * Creates a new pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        pool(unsigned int targetParallelism, unsigned int extraThreads, bool allowWorkOffPoolThreads = true) : pool(
            "",
            targetParallelism, extraThreads,
            defaultThreadFactory,
            allowWorkOffPoolThreads) {
        }

        /**
         * Creates a new pool.
         * @param name The name for this pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         */
        explicit pool(const std::string& name, unsigned int targetParallelism) : pool(
            name, targetParallelism, targetParallelism) {
        }

        /**
         * Creates a new pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
        */
        explicit pool(unsigned int targetParallelism) : pool("", targetParallelism, targetParallelism) {
        }

        /**
         * Creates a new pool with an automatic number of threads.
         * @param name The name for this pool.
         */
        explicit pool(const std::string& name) : pool(name, detectParallelism()) {
        }

        /**
         * Creates a new pool with an automatic number of threads.
        */
        pool() : pool(detectParallelism()) {
        }

        /**
         * Destroys the pool.
         * Shuts down the pool (see pool::shutDown()),
         * then waits for all work previously added to the pool to finish.
         */
        ~pool();

        /**
         * Shuts down the pool.
         * Tasks already running will continue running normally until they complete.
         * Attempting to add tasks to a shut down pool will throw an exception.
         * A shut down pool cannot be restarted.
         * @param cancelUnstarted Whether unstarted pool tasks should be canceled.
         */
        void shut_down(bool cancelUnstarted = false);

        [[nodiscard]] std::string get_name() const;

        /**
         * Adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
        auto add(const TCallback& callback, TArgs... args) -> task<std::invoke_result_t<TCallback, TArgs...>>;

        /**
         * Adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param name The name of the task.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
        auto add(const std::string& name, const TCallback& callback,
                 TArgs... args) -> task<std::invoke_result_t<TCallback, TArgs...>>;

        /**
         * If the pool's queue is not full, adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param newTask The newly created task, if this function returned true.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
        bool try_add(task<std::invoke_result_t<TCallback, TArgs...>>& newTask, const TCallback& callback,
                     TArgs... args);

        /**
         * If the pool's queue is not full, adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param newTask The newly created task, if this function returned true.
         * @param name The name of the task.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
        bool try_add(task<std::invoke_result_t<TCallback, TArgs...>>& newTask, const std::string& name,
                     const TCallback& callback, TArgs... args);

        /**
         * If the pool's queue is not full, adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param newTask The newly created task, if this function returned true.
         * @param timeout The time to wait for the queue to be not full.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return True if the task was created and enqueued, otherwise false.
         */
        template<class Rep, class Period, typename TCallback, typename... TArgs>
        bool try_add_for(task<std::invoke_result_t<TCallback, TArgs...>>& newTask,
                         const std::chrono::duration<Rep, Period>& timeout,
                         const TCallback& callback, TArgs... args);

        /**
         * If the pool's queue is not full, adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param newTask The newly created task, if this function returned true.
         * @param timeout The time to wait for the queue to be not full.
         * @param name The name of the task.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return True if the task was created and enqueued, otherwise false.
         */
        template<class Rep, class Period, typename TCallback, typename... TArgs>
        bool try_add_for(task<std::invoke_result_t<TCallback, TArgs...>>& newTask,
                         const std::chrono::duration<Rep, Period>& timeout,
                         const std::string& name,
                         const TCallback& callback, TArgs... args);


        /**
         * If the pool's queue is not full, adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param newTask The newly created task, if this function returned true.
         * @param timeout The deadline to await for the queue to be not full.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return True if the task was created and enqueued, otherwise false.
         */
        template<class Clock, class Duration, typename TCallback, typename... TArgs>
        bool try_add_until(task<std::invoke_result_t<TCallback, TArgs...>>& newTask,
                           const std::chrono::time_point<Clock, Duration>& timeout,
                           const TCallback& callback, TArgs... args);

        /**
         * If the pool's queue is not full, adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param newTask The newly created task, if this function returned true.
         * @param name The name of the task.
         * @param timeout The deadline to await for the queue to be not full.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return True if the task was created and enqueued, otherwise false.
         */
        template<class Clock, class Duration, typename TCallback, typename... TArgs>
        bool try_add_until(task<std::invoke_result_t<TCallback, TArgs...>>& newTask,
                           const std::chrono::time_point<Clock, Duration>& timeout,
                           const std::string& name,
                           const TCallback& callback, TArgs... args);


        /**
         * Blocks until at least one thread is idle,
         * or until the pool begins to shut down.
         * @return The number of idle threads in this pool.
         */
        unsigned int await_idle_thread();

        /**
         * Blocks until at least one thread is idle,
         * or until the pool begins to shut down,
         * or until the given timeout elapses.
         * @param timeout Time to wait before returning 0 if no threads are idle.
         * @return The number of idle threads in this pool.
         */
        template<class Rep, class Period>
        unsigned int await_idle_thread_for(const std::chrono::duration<Rep, Period>& timeout) {
            return await_idle_thread_until(std::chrono::steady_clock::now() + timeout);
        }

        /**
         * Blocks until at least one thread is idle,
         * or until the pool begins to shut down,
         * or until the given timeout is reached.
         * @param timeout Time to await before returning 0 if no threads are idle.
         * @return The number of idle threads in this pool.
         */
        template<class Clock, class Duration>
        unsigned int await_idle_thread_until(const std::chrono::time_point<Clock, Duration>& timeout) {
            std::unique_lock threadsLock(threadsMutex);
            unsigned int idleThreadCount = 0;
            threadsCv.wait_until(threadsLock, timeout, [&] {
                const auto it = static_cast<int>(targetParallelism) - readyThreads.load(std::memory_order::acquire);
                if (it > 0 || stopping.load(std::memory_order::acquire)) {
                    idleThreadCount = it;
                    return true;
                }
                return false;
            });
            return idleThreadCount;
        }

        /**
         * Blocks until the pool has no current or pending work,
         * or until the pool begins to shut down.
         */
        void await_idle_pool();

        /**
         * Blocks until the pool has no current or pending work,
         * or until the pool begins to shut down,
         * or until the given timeout elapses
         * @param timeout The time to wait.
         * @return True if the pool is idle, otherwise false.
         */
        template<class Rep, class Period>
        bool await_idle_pool_for(const std::chrono::duration<Rep, Period>& timeout) {
            return await_idle_pool_until(std::chrono::steady_clock::now() + timeout);
        }

        /**
         * Blocks until the pool has no current or pending work,
         * or until the pool begins to shut down,
         * or until the given timeout elapses
         * @param timeout The deadline to await.
         * @return True if the pool is idle, otherwise false.
         */
        template<class Clock, class Duration>
        bool await_idle_pool_until(const std::chrono::time_point<Clock, Duration>& timeout) {
            std::unique_lock threadsLock(threadsMutex);
            return threadsCv.wait_until(threadsLock, timeout, [&] {
                return readyThreads.load(std::memory_order::acquire) == 0 && unstarted.empty();
            });
        }

    private:
        template<class Rep, class Period>
        bool wait_for(std::shared_ptr<WorkItem> workItem, const std::chrono::duration<Rep, Period>& timeout) {
            return wait_until(std::move(workItem), std::chrono::steady_clock::now() + timeout);
        }

        template<class Clock, class Duration>
        // NOLINTNEXTLINE(performance-unnecessary-value-param)
        bool wait_until(std::shared_ptr<WorkItem> workItem,
                        const std::chrono::time_point<Clock, Duration>& timeout) {
            auto state = workItem->state.load(std::memory_order::acquire);
            log("Timed wait for task %s (task is %s)",
                workItem->getName().c_str(), WorkItem::workItemStateToString(state));
            if (state == TaskState::Done)
                return true;

            // Block this thread.
            if (executingWorkItem && threadOwningPool == this) {
                --readyThreads;
                threadsCv.notify_one();
            }
            const auto status = workItem->future.wait_until(timeout);
            if (executingWorkItem && threadOwningPool == this) {
                ++readyThreads;
            }

            log("Done with timed wait for %s (task is %s)",
                workItem->getName().c_str(),
                WorkItem::workItemStateToString(workItem->state.load(std::memory_order::acquire))
            );
            return status == std::future_status::ready;
        }

        template<typename TaskIterator>
        static void naive_wait_all(TaskIterator begin, TaskIterator end) {
            if (begin == end)
                return;
            log("naive_wait_all(%s .. %s)", begin->get_name().c_str(), std::prev(end)->get_name().c_str());
            for (auto it = begin; it != end; ++it) {
                it->wait();
            }
        }

        template<typename TaskIterator, class Rep, class Period>
        static bool naive_wait_all_for(TaskIterator begin, TaskIterator end,
                                       const std::chrono::duration<Rep, Period>& timeout) {
            if (begin == end)
                return true;
            log("naive_wait_all_for(%s .. %s)", begin->get_name().c_str(), std::prev(end)->get_name().c_str());
            return naive_wait_all_until(begin, end, std::chrono::steady_clock::now() + timeout);
        }

        template<typename TaskIterator, class Clock, class Duration>
        static bool naive_wait_all_until(TaskIterator begin, TaskIterator end,
                                         const std::chrono::time_point<Clock, Duration>& timeout) {
            if (begin == end)
                return true;
            log("naive_wait_all_until(%s .. %s)", begin->get_name().c_str(), std::prev(end)->get_name().c_str());
            for (auto it = begin; it != end; ++it) {
                if (!it->wait_until(timeout))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool unsafeQueueIsFull() const {
            return maxUnstarted > 0 && unstarted.size() >= maxUnstarted;
        }

        template<class Rep, class Period>
        [[nodiscard]] bool await_queue_not_full(std::unique_lock<std::mutex>& lock,
                                                const std::chrono::duration<Rep, Period>& timeout) {
            if (maxUnstarted == 0)
                return true;
            return threadsCv.wait_for(lock, timeout, [&] { return unstarted.size() < maxUnstarted; });
        }

        template<class Clock, class Duration>
        [[nodiscard]] bool await_queue_not_full(std::unique_lock<std::mutex>& lock,
                                                const std::chrono::time_point<Clock, Duration>& timeout) {
            if (maxUnstarted == 0)
                return true;
            return threadsCv.wait_until(lock, timeout, [&] { return unstarted.size() < maxUnstarted; });
        }

        template<bool blocking, typename TCallback, typename... TArgs>
        auto do_add(std::unique_lock<std::mutex>& lock, const std::string& name, const TCallback& callback,
                    TArgs... args) -> task<std::invoke_result_t<TCallback, TArgs...>>;

        template<typename Timeout, typename TCallback, typename... TArgs>
        bool timed_try_add(task<std::invoke_result_t<TCallback, TArgs...>>& newTask,
                           const Timeout& timeout,
                           const std::string& name,
                           const TCallback& callback, TArgs... args);

    public:
        /**
         * Blocks until all of the given tasks are finished.
         * @tparam TResult Type of the result of each task.
         * @param tasks Array of tasks.
         * @param count Number of tasks.
         */
        template<typename TResult>
        static void wait_all(task<TResult>* tasks, size_t count) {
            wait_all(tasks, tasks + count);
        }

        /**
         * Blocks until all of the given tasks are finished or a timeout occurs.
         * @tparam TResult Type of the result of each task.
         * @param tasks Array of tasks.
         * @param count Number of tasks.
         * @param timeout The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<typename TResult, class Rep, class Period>
        static bool wait_all_for(task<TResult>* tasks, size_t count,
                                 const std::chrono::duration<Rep, Period>& timeout) {
            return wait_all_for(tasks, tasks + count, timeout);
        }

        /**
         * Blocks until all of the given tasks are finished or a timeout occurs.
         * @tparam TResult Type of the result of each task.
         * @param tasks Array of tasks.
         * @param count Number of tasks.
         * @param timeout The point at which to stop waiting.
         * @return False if and only if timeout occurred.
         */
        template<typename TResult, class Clock, class Duration>
        static bool wait_all_until(task<TResult>* tasks, size_t count,
                                   const std::chrono::time_point<Clock, Duration>& timeout) {
            return wait_all_until(tasks, tasks + count, timeout);
        }

        /**
         * Blocks until all of the given tasks are finished.
         * @tparam TaskIterator Type of iterator for task to be awaited.
         * @param begin Iterator pointing to the first task to be awaited.
         * @param end Iterator pointing one past the last task to be awaited.
         */
        template<typename TaskIterator>
        static void wait_all(TaskIterator begin, TaskIterator end);

        /**
         * Blocks until all of the given tasks are finished or a timeout occurs.
         * @tparam TaskIterator Type of iterator for task to be awaited.
         * @param begin Iterator pointing to the first task to be awaited.
         * @param end Iterator pointing one past the last task to be awaited.
         * @param timeout The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<typename TaskIterator, class Rep, class Period>
        static bool wait_all_for(TaskIterator begin, TaskIterator end,
                                 const std::chrono::duration<Rep, Period>& timeout) {
            return naive_wait_all_for(begin, end, timeout);
        }

        /**
         * Blocks until all of the given tasks are finished or a timeout occurs.
         * @tparam TaskIterator Type of iterator for task to be awaited.
         * @param begin Iterator pointing to the first task to be awaited.
         * @param end Iterator pointing one past the last task to be awaited.
         * @param timeout The point at which to stop waiting.
         * @return False if and only if timeout occurred.
         */
        template<typename TaskIterator, class Clock, class Duration>
        static bool wait_all_for(TaskIterator begin, TaskIterator end,
                                 const std::chrono::time_point<Clock, Duration>& timeout) {
            return naive_wait_all_until(begin, end, timeout);
        }

        /**
         * Blocks until all of the given tasks are finished.
         * @tparam IterableTasks Type of iterable thing for tasks to be awaited.
         * @param tasks Iterable thing for tasks to be awaited.
         */
        template<typename IterableTasks>
        static void wait_all(IterableTasks tasks) {
            wait_all(tasks.begin(), tasks.end());
        }

        /**
         * Blocks until all of the given tasks are finished or a timeout occurs.
         * @tparam IterableTasks Type of iterable thing for tasks to be awaited.
         * @param tasks Iterable thing for tasks to be awaited.
         * @param timeout The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<typename IterableTasks, class Rep, class Period>
        static bool wait_all_for(IterableTasks tasks, const std::chrono::duration<Rep, Period>& timeout) {
            return naive_wait_all_for(tasks.begin(), tasks.end(), timeout);
        }

        /**
         * Blocks until all of the given tasks are finished or a timeout occurs.
         * @tparam IterableTasks Type of iterable thing for tasks to be awaited.
         * @param tasks Iterable thing for tasks to be awaited.
         * @param timeout The point at which to stop waiting.
         * @return False if and only if timeout occurred.
         */
        template<typename IterableTasks, class Clock, class Duration>
        static bool wait_all_until(IterableTasks tasks, const std::chrono::time_point<Clock, Duration>& timeout) {
            return naive_wait_all_until(tasks.begin(), tasks.end(), timeout);
        }

        [[nodiscard]] unsigned int get_target_parallelism() const;

        [[nodiscard]] size_t get_queue_size() const;

        [[nodiscard]] FullQueuePolicy get_full_queue_policy() const;
    };

    class pool_builder {
        std::string name;
        unsigned int targetParallelism = 0;
        std::optional<unsigned int> extraThreads;
        bool allowWorkOffPoolThreads = true;
        size_t queueSize = 0;
        FullQueuePolicy fullQueuePolicy = FullQueuePolicy::Block;
        using ThreadFactoryType = std::function<std::thread(const std::function<void()>&)>;
        std::optional<ThreadFactoryType> threadFactory;
        bool builtPool = false;

    public:
        void set_name(const std::string& name) {
            this->name = name;
        }

        [[nodiscard]] std::string get_name() const {
            return this->name;
        }

        void set_target_parallelism(unsigned int targetParallelism) {
            this->targetParallelism = targetParallelism;
        }

        [[nodiscard]] unsigned int get_target_parallelism() const {
            return this->targetParallelism;
        }

        void set_extra_threads(unsigned int extraThreads) {
            this->extraThreads = extraThreads;
        }

        [[nodiscard]] std::optional<unsigned int> get_extra_threads() const {
            return this->extraThreads;
        }

        void set_allow_work_off_pool_threads(bool allowWorkOffPoolThreads) {
            this->allowWorkOffPoolThreads = allowWorkOffPoolThreads;
        }

        [[nodiscard]] bool allow_work_off_pool_threads() const {
            return allowWorkOffPoolThreads;
        }

        void set_queue_size(unsigned int queueSize) {
            this->queueSize = queueSize;
        }

        [[nodiscard]] unsigned int get_queue_size() const {
            return this->queueSize;
        }

        void set_full_queue_policy(FullQueuePolicy fullQueuePolicy) {
            this->fullQueuePolicy = fullQueuePolicy;
        }

        [[nodiscard]] FullQueuePolicy get_full_queue_policy() const {
            return this->fullQueuePolicy;
        }

        void set_thread_factory(ThreadFactoryType threadFactory) {
            this->threadFactory = threadFactory;
        }

        [[nodiscard]] std::optional<ThreadFactoryType> get_thread_factory() {
            return this->threadFactory;
        }

        [[nodiscard]] pool build() {
            if (builtPool)
                throw std::runtime_error("This builder has already built a pool");
            builtPool = true;
            return pool(name, targetParallelism, extraThreads.value_or(targetParallelism),
                        queueSize, fullQueuePolicy,
                        threadFactory.value_or(pool::defaultThreadFactory),
                        allowWorkOffPoolThreads);
        }
    };

    class task_base {
        friend class pool;

    protected:
        std::shared_ptr<pool::WorkItem> wi;

        explicit task_base(const std::shared_ptr<pool::WorkItem>& wrapped)
            : wi(wrapped) {
        }

        void throwIfNull() const {
            if (wi == nullptr)
                throw std::runtime_error("Task is invalid");
        }

    public:
        /**
         * Blocks until this task is complete.
         * @return The result returned from this task.
         */
        void wait() {
            throwIfNull();
            wi->getOwningPool().wait(wi);
        }

        /**
         * Blocks until this task is complete, or until the given timeout elapses.
         * @param timeout The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<class Rep, class Period>
        bool wait_for(const std::chrono::duration<Rep, Period>& timeout) {
            throwIfNull();
            return wi->getOwningPool().wait_for(wi, timeout);
        }

        /**
         * Blocks until this task is complete, or until the given timeout is reached.
         * @param timeout The point at which to stop waiting.
         * @return False if and only if timeout occurred.
         */
        template<class Clock, class Duration>
        bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout) {
            throwIfNull();
            return wi->getOwningPool().wait_until(wi, timeout);
        }

        /**
         * Gets the name of this task.
         * @return The name of this task.
         */
        [[nodiscard]] std::string get_name() const {
            throwIfNull();
            return wi->getName();
        }

        /**
         * Attempts to cancel this task.
         * This will succeed only if the task is unstarted.
         * @return True if this task transitioned from unstarted to canceled.
         */
        bool try_cancel() {
            throwIfNull();
            auto& pool = wi->getOwningPool();
            std::lock_guard lock(pool.threadsMutex);
            if (!wi->trySetCanceled())
                return false;
            pool.unstarted.erase(wi->getIterator());
            return true;
        }

        /**
         * Gets the current state of this task. Does not block.
         * @return The state of this task.
         */
        [[nodiscard]] TaskState get_state() const {
            throwIfNull();
            return wi->getState();
        }

        /**
         * Checks whether this task is unstarted. Does not block.
         * @return A Boolean value indicating whether this task is unstarted.
         */
        [[nodiscard]] bool is_unstarted() const {
            return get_state() == TaskState::Unstarted;
        }

        /**
         * Checks whether this task is executing. Does not block.
         * @return A Boolean value indicating whether this task is executing.
         */
        [[nodiscard]] bool is_executing() const {
            return get_state() == TaskState::Executing;
        }

        /**
         * Checks whether this task is done. Does not block.
         * @return A Boolean value indicating whether this task is done.
         */
        [[nodiscard]] bool is_done() const {
            return get_state() == TaskState::Done;
        }

        /**
         * Checks whether this task is canceled. Does not block.
         * @return A Boolean value indicating whether this task is canceled.
         */
        [[nodiscard]] bool is_canceled() const {
            return get_state() == TaskState::Canceled;
        }
    };

    /**
      * Represents a task that has been submitted to the pool.
      * @tparam TResult The type of the result of this task.
      */
    template<typename TResult>
    class task : public task_base {
        friend class pool;

        explicit task(const std::shared_ptr<pool::WorkItem>& wrapped)
            : task_base(wrapped) {
        }

    public:
        /**
         * Creates an invalid task.
         */
        task() : task_base(nullptr) {
        }

        /**
         * Gets the result returned from this task, waiting if necessary.
         * If the task threw an exception, rethrows it.
         * @return The result returned from this task.
         */
        [[nodiscard]] TResult get() {
            wait();
            return any_cast<TResult>(wi->getResult());
        }
    };

    /**
    * Represents a task that has been submitted to the pool.
    */
    template<>
    class task<void> : public task_base {
        friend class pool;

        explicit task(const std::shared_ptr<pool::WorkItem>& wrapped)
            : task_base(wrapped) {
        }

    public:
        /**
         * Creates an invalid task.
         */
        task() : task_base(nullptr) {
        }

        /**
         * Blocks until this task is complete.
         * If the task threw an exception, rethrows it.
         */
        void get() {
            wait();
            (void)wi->getResult();
        }
    };

    template<typename TCallback, typename... TArgs>
    auto pool::add(const TCallback& callback, TArgs... args) -> task<std::invoke_result_t<TCallback, TArgs...>> {
        return add(generateTaskName(), callback, args...);
    }

    template<typename TCallback, typename... TArgs>
    auto pool::add(const std::string& name, const TCallback& callback,
                   TArgs... args) -> task<std::invoke_result_t<TCallback, TArgs...>> {
        std::unique_lock lock(threadsMutex);
        throwIfStopped();
        return do_add<true>(lock, name, callback, args...);
    }

    template<typename TCallback, typename... TArgs>
    bool pool::try_add(task<std::invoke_result_t<TCallback, TArgs...>>& newTask,
                       const TCallback& callback, TArgs... args) {
        return try_add(newTask, generateTaskName(), callback, args...);
    }

    template<typename TCallback, typename... TArgs>
    bool pool::try_add(task<std::invoke_result_t<TCallback, TArgs...>>& newTask, const std::string& name,
                       const TCallback& callback, TArgs... args) {
        std::unique_lock lock(threadsMutex);
        throwIfStopped();
        if (unsafeQueueIsFull())
            return false;
        newTask = do_add<false>(lock, name, callback, args...);
        return true;
    }

    template<class Rep, class Period, typename TCallback, typename... TArgs>
    bool pool::try_add_for(task<std::invoke_result_t<TCallback, TArgs...>>& newTask,
                           const std::chrono::duration<Rep, Period>& timeout,
                           const TCallback& callback,
                           TArgs... args) {
        return try_add_for(newTask, timeout, generateTaskName(), callback, args...);
    }

    template<class Rep, class Period, typename TCallback, typename... TArgs>
    bool pool::try_add_for(task<std::invoke_result_t<TCallback, TArgs...>>& newTask,
                           const std::chrono::duration<Rep, Period>& timeout, const std::string& name,
                           const TCallback& callback,
                           TArgs... args) {
        return timed_try_add(newTask, timeout, name, callback, args...);
    }

    template<class Clock, class Duration, typename TCallback, typename... TArgs>
    bool pool::try_add_until(task<std::invoke_result_t<TCallback, TArgs...>>& newTask,
                             const std::chrono::time_point<Clock, Duration>& timeout, const TCallback& callback,
                             TArgs... args) {
        return timed_try_add(newTask, timeout, generateTaskName(), callback, args...);
    }

    template<class Clock, class Duration, typename TCallback, typename... TArgs>
    bool pool::try_add_until(task<std::invoke_result_t<TCallback, TArgs...>>& newTask,
                             const std::chrono::time_point<Clock, Duration>& timeout, const std::string& name,
                             const TCallback& callback,
                             TArgs... args) {
        return timed_try_add(newTask, timeout, name, callback, args...);
    }

    template<typename Timeout, typename TCallback, typename... TArgs>
    bool pool::timed_try_add(task<std::invoke_result_t<TCallback, TArgs...>>& newTask, const Timeout& timeout,
                             const std::string& name, const TCallback& callback, TArgs... args) {
        std::unique_lock lock(threadsMutex);
        throwIfStopped();
        if (!await_queue_not_full(lock, timeout))
            return false;
        newTask = do_add<false>(lock, name, callback, args...);
        return true;
    }

    template<bool blocking, typename TCallback, typename... TArgs>
    auto pool::do_add(std::unique_lock<std::mutex>& lock, const std::string& name, const TCallback& callback,
                      TArgs... args) -> task<std::invoke_result_t<TCallback, TArgs...>> {
        using TResult = std::invoke_result_t<TCallback, TArgs...>;
        static_assert(std::is_void_v<TResult> || std::is_copy_constructible_v<TResult>,
                      "Task result must be copy constructible");
        auto wi = std::make_shared<WorkItem>(*this, lastItemId++, name);
        auto* pwi = wi.get(); // Avoid reference cycle
        wi->setCallback(std::packaged_task<std::any()>([=] {
            pwi->throwIfCanceled();
            try {
                if constexpr (std::is_void_v<TResult>) {
                    std::invoke(callback, args...);
                    pwi->state.store(TaskState::Done, std::memory_order::release);
                    return std::any(0); // dummy value
                } else {
                    TResult result = std::invoke(callback, args...);
                    pwi->state.store(TaskState::Done, std::memory_order::release);
                    return std::any(result);
                }
            } catch (...) {
                // Ensure the WorkItem state gets marked as Done even if user code throws.
                pwi->state.store(TaskState::Done, std::memory_order::release);
                throw;
            }
        }));
        if constexpr (blocking) {
            if (unsafeQueueIsFull()) {
                switch (fullQueuePolicy) {
                    default:
                    case FullQueuePolicy::Block:
                        threadsCv.wait(lock, [&] {
                            return unstarted.size() < maxUnstarted;
                        });
                        break;
                    case FullQueuePolicy::DropOld:
                        unstarted.pop_front();
                        break;
                    case FullQueuePolicy::DropNew:
                        wi->trySetCanceled();
                        return task<TResult>(wi);
                }
            }
        }
        const auto it = unstarted.emplace(unstarted.end(), wi);
        wi->enableDeletion(it);
        threadsCv.notify_one();
        return task<TResult>(wi);
    }

    template<typename TaskIterator>
    void pool::wait_all(TaskIterator begin, TaskIterator end) {
        if (begin == end)
            return;
        log("wait_all(%s .. %s)", begin->get_name().c_str(), std::prev(end)->get_name().c_str());

        // For each given item to await,
        // do it synchronously if it's unstarted.
        TaskIterator firstExecuting = end;
        for (auto it = begin; it != end; ++it) {
            auto& item = it->wi;
            bool needRetry;
            do {
                needRetry = false;
                const auto state = item->state.load(std::memory_order::acquire);
                log("%s state is %s", it->get_name().c_str(), WorkItem::workItemStateToString(state));
                switch (state) {
                    default:
                    case TaskState::Done:
                        break;
                    case TaskState::Unstarted: {
                        auto& taskPool = item->getOwningPool();
                        if (threadOwningPool != &taskPool && !taskPool.allowWorkOffPoolThreads) {
                            goto asIfExecuting;
                        }
                        if (item->trySetExecuting()) {
                            // This item was unstarted and now we can execute it synchronously.
                            auto itemValue = item;
                            {
                                std::lock_guard lock(taskPool.threadsMutex);
                                taskPool.unstarted.erase(item->thisIterator);
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
                    case TaskState::Executing:
                    asIfExecuting: {
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
        naive_wait_all(firstExecuting, end);
    }
}
