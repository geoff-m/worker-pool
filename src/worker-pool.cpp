#include "worker-pool/worker-pool.h"

#include <chrono>
#include <sstream>

#ifdef WORKER_POOL_LOGGING
#include <cstdarg>
#include <cstdio>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
#endif

namespace worker_pool {
    [[nodiscard]] unsigned long long getThreadId() {
        #ifdef _WIN32
            return GetCurrentThreadId();
        #else
            return pthread_self();
        #endif
    }

    void log(const char* format...) {
#ifdef WORKER_POOL_LOGGING
        va_list args1;
        va_list args2;
        va_start(args1, format);
        va_copy(args2, args1);
        const auto timeNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        const auto threadKind = threadOwningPool != nullptr ? "pool" : "non-pool";
        const auto threadId = getThreadId();
        const auto prefixLength = snprintf(nullptr, 0, "%lld %s thread %llu: ",
                                           static_cast<long long>(timeNanos), threadKind, threadId);
        const auto payloadLength = vsnprintf(nullptr, 0, format, args1);
        va_end(args1);
        const auto totalLength = prefixLength + payloadLength;
        std::unique_ptr<char[]> buf(new char[totalLength + 1]);
        snprintf(buf.get(), prefixLength + 1, "%lld %s thread %llu: ",
                 static_cast<long long>(timeNanos), threadKind, threadId);
        vsnprintf(buf.get() + prefixLength, payloadLength + 1, format, args2);
        puts(buf.get());
        fflush(stdout);
        va_end(args2);
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

    unsigned int pool::detectParallelism() {
#if defined(__linux__) && !defined(__ANDROID__)
        cpu_set_t cpus;
        if (0 == sched_getaffinity(0, sizeof(cpus), &cpus)) {
            const auto count = CPU_COUNT(&cpus);
            if (count > 0)
                return count;
        }
#endif
        return std::max(1u, std::thread::hardware_concurrency());
    }

    pool::~pool() {
        shutDown(false);
        std::lock_guard lock(threadsMutex);
        for (auto& thread: threads) {
            thread.join();
        }
    }

    void pool::shutDown(bool cancelUnstarted) {
        bool expected = false;
        if (!stopping.compare_exchange_strong(expected, true))
            return;
        {
            std::lock_guard lock(unstartedMutex);
            stopping.store(true, std::memory_order::release);
            if (cancelUnstarted) {
                for (const auto& wi: unstarted)
                    wi->trySetCanceled();
                unstarted.clear();
            }
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
                             std::string name)
        : id(id),
          owningPool(owner),
          name(std::move(name)) {
        state.store(State::Unstarted, std::memory_order::release);
    }

    void pool::WorkItem::enableDeletion(TIterator self) {
        this->thisIterator = self;
    }

    void pool::WorkItem::setCallback(std::packaged_task<std::any()>&& callback) {
        task = std::move(callback),
                future = task.get_future().share();
    }

    void pool::WorkItem::throwIfCanceled() {
        if (state.load(std::memory_order::acquire) == State::Canceled)
            throw std::runtime_error("This task has been canceled");
    }

    bool pool::WorkItem::operator==(const WorkItem& other) const {
        return id == other.id && &owningPool == &other.owningPool;
    }

    bool pool::WorkItem::trySetExecuting() {
        State oldState = State::Unstarted;
        if (state.compare_exchange_strong(oldState, State::Executing)) {
            //log("trySetExecuting succeeded for task %s", getName().c_str());
            return true;
        }
        //log("trySetExecuting failed for task %s", getName().c_str());
        return false;
    }

    bool pool::WorkItem::trySetCanceled() {
        State oldState = State::Unstarted;
        if (!state.compare_exchange_strong(oldState, State::Canceled))
            return false;
        execute();
        return true;
    }

    void pool::WorkItem::execute() {
#ifdef WORKER_POOL_DEADLOCK_DETECTION
        auto* oldExecuting = executingWorkItem;
        executingWorkItem = this;
#endif
        log("Beginning task %s", getName().c_str());
        task();
        state.store(State::Done, std::memory_order::release);
#ifdef WORKER_POOL_DEADLOCK_DETECTION
        executingWorkItem = oldExecuting;
#endif
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

    pool::WorkItem::TIterator pool::WorkItem::getIterator() const {
        return thisIterator;
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
            //log("Not creating extra thread because no quota");
        } else {
            //log("Not creating extra thread because waiter is not a pool thread");
        }
    }

#ifdef WORKER_POOL_DEADLOCK_DETECTION
    thread_local pool::WorkItem* pool::executingWorkItem = nullptr;

    std::string pool::formatWaitChain(const WorkItem& wi) {
        std::stringstream ss;
        ss << wi.getName();
        auto* p = wi.waitingFor;
        while (p) {
            ss << " -> " << p->getName();
            p = p->waitingFor;
        }
        return ss.str();
    }

    static std::mutex deadlockCheckMutex;
    // The caller should hold deadlockCheckMutex.
    void pool::checkDeadlock(WorkItem& toAwait) {
        if (!executingWorkItem)
            return;
        log("%s is about to start waiting for %s", executingWorkItem->getName().c_str(),
            toAwait.getName().c_str());
        // Walk the wait chain, looking for the WorkItem we're currently executing.
        auto* waitingFor = &toAwait;
        while (waitingFor) {
            auto* waitingForNext = waitingFor->waitingFor;
            log("%s is waiting for %s", waitingFor->name.c_str(),
                waitingForNext ? waitingForNext->name.c_str() : "nothing");
            if (waitingFor == executingWorkItem) {
#ifdef WORKER_POOL_DEADLOCK_DETECTION_TERMINATE
                std::terminate();
#else
                std::string msg = "The requested wait would deadlock: ";
                msg += executingWorkItem->getName();
                msg += " would wait for itself via ";
                msg += formatWaitChain(toAwait);
                log("Throwing exception: %s", msg.c_str());
                throw deadlock_exception(msg);
#endif
            }
            waitingFor = waitingForNext;
        }
    }
#endif

    void pool::wait(std::shared_ptr<WorkItem> workItem) {
#ifdef WORKER_POOL_DEADLOCK_DETECTION
        std::unique_lock deadlockCheckLock(deadlockCheckMutex, std::defer_lock);
        WorkItem* oldWaitingFor = nullptr;
        if (executingWorkItem) {
            deadlockCheckLock.lock();
        }
#define PUSH_WAITING_FOR do { \
if (executingWorkItem) { \
oldWaitingFor = executingWorkItem->waitingFor; \
executingWorkItem->waitingFor = workItem.get(); \
log("%s is now awaiting %s", executingWorkItem->getName().c_str(), workItem->getName().c_str()); \
deadlockCheckLock.unlock(); \
} } while (0)
#define POP_WAITING_FOR do { if (executingWorkItem) {\
deadlockCheckLock.lock(); \
executingWorkItem->waitingFor = oldWaitingFor; \
deadlockCheckLock.unlock(); \
} } while (0)
#else
#define PUSH_WAITING_FOR
#define POP_WAITING_FOR
#endif

        // retry loop
        while (true) {
            auto state = workItem->state.load(std::memory_order::acquire);
            log("Untimed wait for task %s (task is %s)",
                workItem->getName().c_str(), WorkItem::workItemStateToString(state));
            switch (state) {
                default:
                case WorkItem::State::Done:
                    // Waiting for an item that's already done.
                    // There's no way this could lead to a deadlock.
                    return;
                case WorkItem::State::Unstarted: {
                    // Waiting for an item that hasn't begun to be executed yet.
                    FAIL_IF_WAITING_WILL_DEADLOCK(*workItem);
                    if (!allowWorkOffPoolThreads && threadOwningPool != this) {
                        PUSH_WAITING_FOR;
                        workItem->future.wait();
                        POP_WAITING_FOR;
                        return;
                    }
                    // Execute it synchronously.
                    bool doExecute = false;
                    {
                        std::lock_guard lock(unstartedMutex);
                        if (workItem->trySetExecuting()) {
                            unstarted.erase(workItem->thisIterator);
                            doExecute = true;
                        }
                    }
                    if (doExecute) {
                        PUSH_WAITING_FOR;
                        workItem->execute();
                        POP_WAITING_FOR;
                        return;
                    }
                    continue; // retry wait on this item.
                }
                case WorkItem::State::Executing: {
                    // Waiting for an item that's currently being executed.
                    FAIL_IF_WAITING_WILL_DEADLOCK(*workItem);
                    PUSH_WAITING_FOR;
                    maybeAddThreadBeforeWait(*workItem);
                    // Block this thread.
                    workItem->future.wait();
                    POP_WAITING_FOR;
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
            if (item == unstarted.end()) {
                // Failed to mark any item as executing.
                std::this_thread::yield();
                continue;
            }
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
