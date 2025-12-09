#include "../include/worker-pool/worker-pool.h"

#ifdef WORKER_POOL_LOGGING
#include <cstdarg>
#include <cstdio>
#endif

namespace worker_pool {
    void log(const char* format...) {
#ifdef WORKER_POOL_LOGGING
        va_list args;
        va_start(args, format);
        char buf[256];
        buf[sizeof(buf) - 1] = '\0';
        const auto timeNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        const auto threadKind = worker_pool::threadOwningPool != nullptr ? "pool" : "non-pool";
        const auto prefixLength = snprintf(buf, sizeof(buf), "%ld %s thread %lu: ",
                                           timeNanos, threadKind, pthread_self());
        vsnprintf(buf + prefixLength, sizeof(buf) - prefixLength, format, args);
        puts(buf);
        va_end(args);
#endif
    }

    const char* pool::WorkItem::workItemStateToString(State state) {
        switch (state) {
            case State::Unstarted:
                return "Unstarted";
            case State::Executing:
                return "Executing";
            case State::Done:
                return "Done";
            default:
                return "Unknown";
        }
    }


    pool::~pool() {
        shutDown();
        std::lock_guard lock(threadsMutex);
        for (auto& thread: threads) {
            thread.join();
        }
    }

    void pool::shutDown() {
        {
            std::lock_guard lock(unstartedMutex);
            stopping.store(true, std::memory_order::release);
        }
        cv.notify_all();
    }

    void pool::throwIfStopped() const {
        if (stopping.load(std::memory_order::acquire))
            throw std::runtime_error("Cannot add to stopped thread WorkerPool");
    }

    void pool::unsafeAddThread() {
        readyThreads.fetch_add(1);
        threads.emplace_back(threadFactory([this] { work(); }));
    }

    pool::WorkItem::WorkItem(pool& owner,
                             size_t id,
                             std::packaged_task<std::any()> task,
                             std::string name)
        : id(id),
          owningPool(owner),
          task(std::move(task)),
          future(this->task.get_future().share()),
    name(std::move(name)){
        state.store(State::Unstarted, std::memory_order::release);
    }

    void pool::WorkItem::enableDeletion(TIterator self) {
        this->thisIterator = self;
    }

    bool pool::WorkItem::operator==(const WorkItem& other) const {
        return id == other.id && &owningPool == &other.owningPool;
    }

    bool pool::WorkItem::trySetExecuting() {
        State oldState = State::Unstarted;
        if (state.compare_exchange_strong(oldState, State::Executing)) {
            log("trySetExecuting succeeded for task %s", getName().c_str());
            return true;
        }
        log("trySetExecuting failed for task %s", getName().c_str());
        return false;
    }

    void pool::WorkItem::execute() {
        log("Beginning task %s", getName().c_str());
        task();
        state.store(State::Done, std::memory_order::release);
        log("Finished task %s", getName().c_str());
    }

    pool& pool::WorkItem::getOwningPool() const {
        return owningPool;
    }

    std::any pool::WorkItem::getResult() {
        return future.get();
    }

    std::string pool::WorkItem::getName() const {
        return name;
    }

    void pool::maybeAddThreadBeforeWait(const WorkItem& workItem) {
        if (&workItem.owningPool == this && threadOwningPool == this) {
            // We are about to block a pool thread, so consider creating an extra thread.
            if (readyThreads.load(std::memory_order::acquire) < targetParallelism + maxWaiterThreads) {
                // We have quota to create an extra thread to make up for waiting.
                log("Creating extra thread");
                std::lock_guard lock(threadsMutex);
                unsafeAddThread();
            }
            log("Not creating extra thread because no quota");
        } else {
            log("Not creating extra thread because waiter is not a pool thread");
        }
    }

    void pool::wait(WorkItem& workItem) {
        // retry loop
        while (true) {
            auto state = workItem.state.load(std::memory_order::acquire);
            log("Untimed wait for task %s (task is %s)",
                workItem.getName().c_str(), WorkItem::workItemStateToString(state));
            switch (state) {
                default:
                case WorkItem::State::Done:
                    // Waiting for an item that's already done.
                    // Return immediately.
                    return;
                case WorkItem::State::Unstarted: {
                    // Waiting for an item that hasn't begun to be executed yet.
                    if (!allowWorkOffPoolThreads && threadOwningPool != this) {
                        workItem.future.wait();
                        return;
                    }
                    // Execute it synchronously.
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
                    maybeAddThreadBeforeWait(workItem);
                    // Block this thread.
                    workItem.future.wait();
                    return;
                }
            }
        }
    }

    bool pool::threadIsExtra() const {
        return readyThreads.load(std::memory_order::acquire) > targetParallelism;
    }

    bool pool::threadShouldExit() const {
        return stopping.load(std::memory_order::acquire) || threadIsExtra();
    }

    void pool::work() {
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
            auto itemValue = *item; // NOLINT(*-unnecessary-copy-initialization)
            unstarted.erase(item);
            unstartedLock.unlock();
            itemValue->execute();
        }
    }
}
