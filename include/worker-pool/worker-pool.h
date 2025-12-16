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

#if defined(WORKER_POOL_DEADLOCK_DETECTION_STRICT) && !defined(WORKER_POOL_DEADLOCK_DETECTION)
#define WORKER_POOL_DEADLOCK_DETECTION
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
            : runtime_error("Task has been canceled") {}
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

    /**
     * A thread pool to which tasks can be added as callbacks.
     * The pool eventually executes all tasks ever added to it.
     * The pool tends to begin executing tasks in FIFO order, but this is not guaranteed.
     */
    class pool {
        class WorkItem {
            friend class pool;

            [[nodiscard]] static const char* workItemStateToString(TaskState state);

            size_t id;
            std::atomic<TaskState> state;
            pool& owningPool;
            std::packaged_task<std::any()> task;
            std::shared_future<std::any> future;
            const std::string name;
            using TIterator = std::list<std::shared_ptr<WorkItem>>::iterator;
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

#if defined(WORKER_POOL_DEADLOCK_DETECTION) || defined(WORKER_POOL_DEADLOCK_DETECTION_STRICT)
            WorkItem* waitingFor = nullptr;
#endif
        };

        template<typename T>
        friend class task;
        friend class WorkItem;
        const std::string name;
        static std::atomic<unsigned int> id;
        std::atomic<size_t> addedTaskCount = 0;
        std::mutex threadsMutex;
        std::atomic<unsigned int> readyThreads; // The number of threads that are doing work or are ready to do so.
        std::list<std::thread> threads;
        const unsigned int targetParallelism;
        const unsigned int maxWaiterThreads;
        const std::function<std::thread(std::function<void()>)> threadFactory;
        const bool allowWorkOffPoolThreads;

        [[nodiscard]] static std::string generatePoolName();

        [[nodiscard]] std::string generateTaskName();

    public:
        /**
         * Creates a new pool.
         * @param name The name for this pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param threadFactory A callable like std::thread::thread(callback) which the pool will use
         * to create threads when needed.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        template<typename ThreadFactory>
        pool(std::string name, unsigned int targetParallelism, unsigned int extraThreads, ThreadFactory threadFactory,
             bool allowWorkOffPoolThreads = true)
            : name(name.empty() ? generatePoolName() : std::move(name)),
              targetParallelism(targetParallelism),
              maxWaiterThreads(extraThreads),
              threadFactory([&threadFactory](const std::function<void()>& callback) {
                  return threadFactory(callback);
              }),
              allowWorkOffPoolThreads(allowWorkOffPoolThreads) {
            if (targetParallelism <= 0)
                throw std::invalid_argument("Target parallelism must be at least 1");
            std::lock_guard lock(threadsMutex);
            for (unsigned int i = 0; i < targetParallelism; i++)
                unsafeAddThread();
        }

        /**
         * Creates a new pool.
         * @param targetParallelism The target number of threads to use for simultaneous work.
         * @param extraThreads The maximum number of extra threads to create when wait is called by a pool thread.
         * @param threadFactory A callable like std::thread::thread(callback) which the pool will use
         * to create threads when needed.
         * @param allowWorkOffPoolThreads Whether the pool is allowed to execute callbacks in non-pool waiter threads.
         */
        template<typename ThreadFactory>
        pool(unsigned int targetParallelism, unsigned int extraThreads, ThreadFactory threadFactory,
             bool allowWorkOffPoolThreads = true)
            : pool("", targetParallelism, extraThreads, threadFactory, allowWorkOffPoolThreads) {
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
            [](const std::function<void()>& callback) { return std::thread(callback); },
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
            [](const std::function<void()>& callback) { return std::thread(callback); },
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
        void shutDown(bool cancelUnstarted = false);

        [[nodiscard]] std::string get_name() const;

    private:
        std::condition_variable cv;
        std::mutex unstartedMutex;
        std::list<std::shared_ptr<WorkItem>> unstarted;
        std::atomic<bool> stopping = false;

        void throwIfStopped() const;

        void unsafeAddThread();

        [[nodiscard]] static unsigned int detectParallelism();

    public:
        /**
         * Adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
        auto add(TCallback callback, TArgs... args) -> task<decltype(std::invoke(callback, args...))>;

        /**
         * Adds a callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param name Name of the task
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
        auto add(std::string name, TCallback callback, TArgs... args) -> task<decltype(std::invoke(callback, args...))>;

        /**
         * Adds a void callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
            requires invocable_returns_void<TCallback, TArgs...>
        auto add(TCallback callback, TArgs... args) -> task<void>;

        /**
         * Adds a void callback to the pool.
         * @tparam TCallback Type of the callback function.
         * @tparam TArgs Types of the arguments to the callback.
         * @param name Name of the task
         * @param callback The function that will perform the work.
         * @param args The arguments, if any, to the callback function.
         * @return A task representing the work associated with this call.
         */
        template<typename TCallback, typename... TArgs>
            requires invocable_returns_void<TCallback, TArgs...>
        auto add(std::string name, TCallback callback, TArgs... args) -> task<void>;

    private:
        size_t lastItemId = 0;

        [[nodiscard]] bool threadIsExtra() const;

        [[nodiscard]] bool threadShouldExit() const;

        void work();

        void wait(std::shared_ptr<WorkItem> workItem);

        void maybeAddThreadBeforeWait(const WorkItem& workItem);

        template<class Rep, class Period>
        bool wait_for(std::shared_ptr<WorkItem> workItem, const std::chrono::duration<Rep, Period>& timeout_duration) {
            return wait_until(workItem, std::chrono::steady_clock::now() + timeout_duration);
        }

        template<class Clock, class Duration>
        bool wait_until(std::shared_ptr<WorkItem> workItem,
                        const std::chrono::time_point<Clock, Duration>& timeout_time) {
            auto state = workItem->state.load(std::memory_order::acquire);
            log("Timed wait for task %s (task is %s)",
                workItem->getName().c_str(), WorkItem::workItemStateToString(state));
            if (state == TaskState::Done)
                return true;

            maybeAddThreadBeforeWait(*workItem);
            // Block this thread.
            const auto status = workItem->future.wait_until(timeout_time);

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
                                       const std::chrono::duration<Rep, Period>& timeout_duration) {
            if (begin == end)
                return true;
            log("naive_wait_all_for(%s .. %s)", begin->get_name().c_str(), std::prev(end)->get_name().c_str());
            return naive_wait_all_until(begin, end, std::chrono::steady_clock::now() + timeout_duration);
        }

        template<typename TaskIterator, class Clock, class Duration>
        static bool naive_wait_all_until(TaskIterator begin, TaskIterator end,
                                         const std::chrono::time_point<Clock, Duration>& timeout_time) {
            if (begin == end)
                return true;
            log("naive_wait_all_until(%s .. %s)", begin->get_name().c_str(), std::prev(end)->get_name().c_str());
            for (auto it = begin; it != end; ++it) {
                if (!it->wait_until(timeout_time))
                    return false;
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
        static void wait_all(task<TResult>* tasks, size_t count) {
            wait_all(tasks, tasks + count);
        }

        /**
         * Blocks until all of the given tasks are finished or a timeout occurs.
         * @tparam TResult Type of the result of each task.
         * @param tasks Array of tasks.
         * @param count Number of tasks.
         * @param timeout_duration The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<typename TResult, class Rep, class Period>
        static bool wait_all_for(task<TResult>* tasks, size_t count,
                                 const std::chrono::duration<Rep, Period>& timeout_duration) {
            return wait_all_for(tasks, tasks + count, timeout_duration);
        }

        /**
         * Blocks until all of the given tasks are finished or a timeout occurs.
         * @tparam TResult Type of the result of each task.
         * @param tasks Array of tasks.
         * @param count Number of tasks.
         * @param timeout_time The point at which to stop waiting.
         * @return False if and only if timeout occurred.
         */
        template<typename TResult, class Clock, class Duration>
        static bool wait_all_until(task<TResult>* tasks, size_t count,
                                   const std::chrono::time_point<Clock, Duration>& timeout_time) {
            return wait_all_until(tasks, tasks + count, timeout_time);
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
         * @param timeout_duration The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<typename TaskIterator, class Rep, class Period>
        static bool wait_all_for(TaskIterator begin, TaskIterator end,
                                 const std::chrono::duration<Rep, Period>& timeout_duration) {
            return naive_wait_all_for(begin, end, timeout_duration);
        }

        /**
         * Blocks until all of the given tasks are finished or a timeout occurs.
         * @tparam TaskIterator Type of iterator for task to be awaited.
         * @param begin Iterator pointing to the first task to be awaited.
         * @param end Iterator pointing one past the last task to be awaited.
         * @param timeout_time The point at which to stop waiting.
         * @return False if and only if timeout occurred.
         */
        template<typename TaskIterator, class Clock, class Duration>
        static bool wait_all_for(TaskIterator begin, TaskIterator end,
                                 const std::chrono::time_point<Clock, Duration>& timeout_time) {
            return naive_wait_all_until(begin, end, timeout_time);
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
         * @param timeout_duration The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<typename IterableTasks, class Rep, class Period>
        static bool wait_all_for(IterableTasks tasks, const std::chrono::duration<Rep, Period>& timeout_duration) {
            return naive_wait_all_for(tasks.begin(), tasks.end(), timeout_duration);
        }

        /**
         * Blocks until all of the given tasks are finished or a timeout occurs.
         * @tparam IterableTasks Type of iterable thing for tasks to be awaited.
         * @param tasks Iterable thing for tasks to be awaited.
         * @param timeout_time The point at which to stop waiting.
         * @return False if and only if timeout occurred.
         */
        template<typename IterableTasks, class Clock, class Duration>
        static bool wait_all_until(IterableTasks tasks, const std::chrono::time_point<Clock, Duration>& timeout_time) {
            return naive_wait_all_until(tasks.begin(), tasks.end(), timeout_time);
        }

    private:
#ifdef WORKER_POOL_DEADLOCK_DETECTION
        static thread_local WorkItem* executingWorkItem;

        static void checkDeadlock(WorkItem& toAwait);

        [[nodiscard]] static std::string formatWaitChain(const WorkItem& wi);

#define FAIL_IF_WAITING_WILL_DEADLOCK(toAwait) checkDeadlock(toAwait)
#else
#define FAIL_IF_WAITING_WILL_DEADLOCK(toAwait)
#endif
    };

    /**
      * Represents a task that has been submitted to the pool.
      * @tparam TResult The type of the result of this task.
      */
    template<typename TResult>
    class task {
        friend class pool;
        std::shared_ptr<pool::WorkItem> wi;

        explicit task(const std::shared_ptr<pool::WorkItem>& wrapped)
            : wi(wrapped) {
        }

    public:
        /**
         * Blocks until this task is complete.
         * Rethrows whatever exception the asynchronous operation threw, if any.
         * @return The result returned from this task.
         */
        void wait() {
            wi->getOwningPool().wait(wi);
        }

        /**
         * Blocks until this task is complete, or until the given timeout elapses.
         * @param timeout_duration The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<class Rep, class Period>
        bool wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
            return wi->getOwningPool().wait_for(wi, timeout_duration);
        }

        /**
         * Blocks until this task is complete, or until the given timeout is reached.
         * @param timeout_time The point at which to stop waiting.
         * @return False if and only if timeout occurred.
         */
        template<class Clock, class Duration>
        bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
            return wi->getOwningPool().wait_until(wi, timeout_time);
        }

        /**
         * Gets the result returned from this task, waiting if necessary.
         * @return The result returned from this task.
         */
        [[nodiscard]] TResult get() {
            wait();
            return any_cast<TResult>(wi->getResult());
        }

        /**
         * Gets the name of this task.
         * @return The name of this task.
         */
        [[nodiscard]] std::string get_name() const {
            return wi->getName();
        }

        /**
         * Attempts to cancel this task.
         * This will succeed only if the task is unstarted.
         * @return True if this task transitioned from unstarted to canceled.
         */
        bool try_cancel() {
            auto& pool = wi->getOwningPool();
            std::lock_guard lock(pool.unstartedMutex);
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
    */
    template<>
    class task<void> {
        friend class pool;
        std::shared_ptr<pool::WorkItem> wi;

        explicit task(const std::shared_ptr<pool::WorkItem>& wrapped)
            : wi(wrapped) {
        }

    public:
        /**
         * Blocks until this task is complete.
         * Rethrows whatever exception the asynchronous operation threw, if any.
         */
        void get() {
            wait();
            (void) wi->getResult();
        }

        /**
         * Blocks until this task is complete.
         */
        void wait() {
            wi->getOwningPool().wait(wi);
        }

        /**
         * Blocks until this task is complete or a timeout occurs.
         * @param timeout_duration The maximum amount of time to wait.
         * @return False if and only if timeout occurred.
         */
        template<class Rep, class Period>
        bool wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
            return wi->getOwningPool().wait_for(wi, timeout_duration);
        }

        /**
         * Blocks until this task is complete or a timeout occurs.
         * @param timeout_time The point at which to stop waiting.
         * @return False if and only if timeout occurred.
         */
        template<class Clock, class Duration>
        bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
            return wi->getOwningPool().wait_until(wi, timeout_time);
        }

        /**
        * Gets the name of this task.
        * @return The name of this task.
        */
        [[nodiscard]] std::string get_name() const {
            return wi->getName();
        }

        /**
         * Attempts to cancel this task.
         * This will succeed only if the task is unstarted.
         * @return True if this task transitioned from unstarted to canceled.
         */
        bool try_cancel() {
            auto& pool = wi->getOwningPool();
            std::lock_guard lock(pool.unstartedMutex);
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

    template<typename TCallback, typename... TArgs>
    auto pool::add(TCallback callback, TArgs... args) -> task<decltype(std::invoke(callback, args...))> {
        return add(generateTaskName(), callback, args...);
    }

    template<typename TCallback, typename... TArgs>
    auto pool::add(std::string name, TCallback callback,
                   TArgs... args) -> task<decltype(std::invoke(callback, args...))> {
        using TResult = decltype(std::invoke(callback, args...));
        std::lock_guard lock(unstartedMutex);
        throwIfStopped();
        auto wi = std::make_shared<WorkItem>(*this,
                                             lastItemId++, name);
        auto* pwi = wi.get(); // Avoid reference cycle
        wi->setCallback(std::packaged_task<std::any()>([=] {
            pwi->throwIfCanceled();
            try {
                TResult result = std::invoke(callback, args...);
                pwi->state.store(TaskState::Done, std::memory_order::release);
                return std::any(result);
            } catch (...) {
                // Ensure the WorkItem state gets marked as Done even if user code throws.
                pwi->state.store(TaskState::Done, std::memory_order::release);
                throw;
            }
        }));
        const auto it = unstarted.emplace(unstarted.end(), wi);
        wi->enableDeletion(it);
        cv.notify_one();
        return task<TResult>(wi);
    }

    template<typename TCallback, typename... TArgs>
        requires invocable_returns_void<TCallback, TArgs...>
    auto pool::add(TCallback callback, TArgs... args) -> task<void> {
        return add(generateTaskName(), callback, args...);
    }

    template<typename TCallback, typename... TArgs>
        requires invocable_returns_void<TCallback, TArgs...>
    auto pool::add(std::string name, TCallback callback, TArgs... args) -> task<void> {
        std::lock_guard lock(unstartedMutex);
        throwIfStopped();
        auto wi = std::make_shared<WorkItem>(*this, lastItemId++, name);
        auto* pwi = wi.get(); // Avoid reference cycle
        wi->setCallback(std::packaged_task<std::any()>([=] {
            pwi->throwIfCanceled();
            try {
                std::invoke(callback, args...);
                pwi->state.store(TaskState::Done, std::memory_order::release);
                return std::any(0); // dummy value
            } catch (...) {
                // Ensure the WorkItem state gets marked as Done even if user code throws.
                pwi->state.store(TaskState::Done, std::memory_order::release);
                throw;
            }
        }));
        const auto it = unstarted.emplace(unstarted.end(), wi);
        wi->enableDeletion(it);
        cv.notify_one();
        return task<void>(wi);
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
                                std::lock_guard lock(taskPool.unstartedMutex);
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
