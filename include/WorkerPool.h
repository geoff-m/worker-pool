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
#include <cstdio>
#include <chrono>
#include "WorkerPool.h"


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
static thread_local WorkerPool* threadOwningPool;

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
        stopping.store(true, std::memory_order::release);
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
        threads.emplace_back();
        auto end = std::prev(threads.end());
        new(&*end) std::thread([this, end] { work(end); });
    }

    void unsafeDeleteThread(const std::list<std::thread>::iterator thread) {
        readyThreads.fetch_sub(1);
        threads.erase(thread);
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

        void execute() {
            // Remove from pool's unstarted list
            {
                std::scoped_lock lock(owningPool.unstartedMutex);
                State oldState = State::Unstarted;
                if (!state.compare_exchange_strong(oldState, State::Executing))
                    return;
                owningPool.unstarted.erase(thisIterator);
            }
            printf("%s Beginning task %p on pool thread %d\n",
                   std::to_string(
                       std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count()).c_str(),
                   reinterpret_cast<const void*>(this), gettid());

            task();
            state.store(State::Done, std::memory_order::release);
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
            // (old) naive wait
            //wi->future.wait();

            // (new) pool wait
            wi->owningPool.wait(*wi);
        }

        TResult getResult() {
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
        unstarted.emplace_back(wi);
        wi->enableDeletion(std::prev(unstarted.end()));
        cv.notify_one();
        return Task<TResult>(wi);
        // return std::async(std::launch::deferred, [wi, this] {
        //     auto future = wi->task.get_future();
        //     future.wait();
        //     auto value = future.get();
        //     std::lock_guard lock(startedMutex);
        //     started.erase(wi);
        //     return any_cast<TResult>(value);
        // });
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
        unstarted.emplace_back(wi);
        wi->enableDeletion(std::prev(unstarted.end()));

        cv.notify_one();
        return Task<void>(wi);
        // return std::async(std::launch::deferred, [wi, this] {
        //     const auto future = wi->task.get_future();
        //     future.wait();
        //     std::lock_guard lock(startedMutex);
        //     started.erase(wi);
        // });
    }

private:
    size_t lastItemId = 0;

public:
    // todo: rework this class to return WorkItems (make WorkItem public),
    // instead of std::future so that this function can be used on them?
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
                // {
                //     std::lock_guard lock(workItem.owningPool.unstartedMutex);
                //     WorkItem::State oldState = WorkItem::State::Unstarted;
                //     if (!workItem.state.compare_exchange_strong(oldState, WorkItem::State::Executing))
                //         return wait(workItem); // Failed to change state to executing. Retry this operation.
                // }
                printf("Synchronously executing task\n");
                workItem.execute();
                return;
            }
            case WorkItem::State::Executing: {
                // Waiting for an item that's currently being executed.
                if (&workItem.owningPool == this && threadOwningPool == this)
                // i don't think we really care about this condition, just the one that follows.
                {
                    // We are about to block a pool thread, so consider creating an extra thread.
                    if (readyThreads.load(std::memory_order::acquire) < targetParallelism + maxWaiterThreads) {
                        // We have quota to create an extra thread to make up for waiting.
                        printf("wait called from pool thread: creating extra thread\n");
                        std::lock_guard lock(threadsMutex);
                        unsafeAddThread();
                    }
                    printf("wait called from pool thread: not creating extra thread\n");
                } else {
                    printf("wait called from non-pool thread\n");
                }
                // Block this thread.
                workItem.future.wait();
                break;
            }
        }
    }

    [[nodiscard]] bool threadIsExtra() const {
        return readyThreads.load(std::memory_order::acquire) > targetParallelism;
    }

    [[nodiscard]] bool threadShouldExit() const {
        return stopping.load(std::memory_order::acquire) || threadIsExtra();
    }

    void work(decltype(threads)::iterator thisThread) {
        threadOwningPool = this;
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
                    std::this_thread::yield();
                }
                // We hold the thread lock.
                if (!stopping.load(std::memory_order::acquire)) {
                    unsafeDeleteThread(thisThread);
                }
                return;
            }

            auto item = unstarted.begin();
            unstartedLock.unlock();

            item->get()->execute();
        }
    }
};
