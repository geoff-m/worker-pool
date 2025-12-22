#include "worker-pool/worker-pool.h"

#include <cassert>
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
#ifdef WORKER_POOL_LOGGING
    [[nodiscard]] unsigned long long getThreadId() {
#ifdef _WIN32
        return GetCurrentThreadId();
#else
        return pthread_self();
#endif
    }
#endif

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

    const char* pool::WorkItem::workItemStateToString(TaskState state) {
        switch (state) {
            case TaskState::Unstarted:
                return "Unstarted";
            case TaskState::Executing:
                return "Executing";
            case TaskState::Done:
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

    std::atomic<unsigned int> pool::id = 0;

    std::string pool::generatePoolName() {
        return std::string("pool") + std::to_string(id++);
    }

    std::string pool::generateTaskName() {
        return get_name() + "_task" + std::to_string(addedTaskCount++);
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

    std::string pool::get_name() const {
        return name;
    }

    void pool::throwIfStopped() const {
        if (stopping.load(std::memory_order::acquire))
            throw std::runtime_error("Cannot add to stopped thread WorkerPool");
    }

    pool::WorkItem::WorkItem(pool& owner,
                             size_t id,
                             std::string name)
        : id(id),
          owningPool(owner),
          name(std::move(name)) {
        state.store(TaskState::Unstarted, std::memory_order::release);
    }

    void pool::WorkItem::enableDeletion(TIterator self) {
        this->thisIterator = self;
    }

    void pool::WorkItem::setCallback(std::packaged_task<std::any()>&& callback) {
        task = std::move(callback),
                future = task.get_future().share();
    }

    void pool::WorkItem::throwIfCanceled() {
        if (state.load(std::memory_order::acquire) == TaskState::Canceled)
            throw canceled_exception();
    }

    bool pool::WorkItem::operator==(const WorkItem& other) const {
        return id == other.id && &owningPool == &other.owningPool;
    }

    bool pool::WorkItem::trySetExecuting() {
        auto oldState = TaskState::Unstarted;
        if (state.compare_exchange_strong(oldState, TaskState::Executing)) {
            //log("trySetExecuting succeeded for task %s", getName().c_str());
            return true;
        }
        //log("trySetExecuting failed for task %s", getName().c_str());
        return false;
    }

    bool pool::WorkItem::trySetCanceled() {
        auto oldState = TaskState::Unstarted;
        if (!state.compare_exchange_strong(oldState, TaskState::Canceled))
            return false;
        // This will throw an exception indicating we're canceled.
        // This is needed so that the packaged_task/future is marked complete.
        execute();
        return true;
    }

    void pool::WorkItem::execute() {
        auto* oldExecuting = executingWorkItem;
        executingWorkItem = this;
        log("Beginning task %s", getName().c_str());
        task();
        executingWorkItem = oldExecuting;
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

    TaskState pool::WorkItem::getState() const {
        return state.load(std::memory_order::acquire);
    }

    thread_local pool::WorkItem* pool::executingWorkItem = nullptr;

#ifdef WORKER_POOL_DEADLOCK_DETECTION
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
                if (waitingFor == &toAwait) {
                    msg += " would wait for itself";
                } else {
                    msg += " would wait for itself via ";
                    msg += formatWaitChain(toAwait);
                }
                log("Throwing exception: %s", msg.c_str());
                throw deadlock_exception(msg);
#endif
            }
            waitingFor = waitingForNext;
        }
    }
#endif

    // NOLINTNEXTLINE(performance-unnecessary-value-param)
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
                case TaskState::Done:
                    // Waiting for an item that's already done.
                    // There's no way this could lead to a deadlock.
                    return;
                case TaskState::Unstarted: {
                    // Waiting for an item that hasn't begun to be executed yet.
                    FAIL_IF_WAITING_WILL_DEADLOCK(*workItem);
                    if (!allowWorkOffPoolThreads && threadOwningPool != this) {
                        PUSH_WAITING_FOR;
                        workItem->future.wait();
                        POP_WAITING_FOR;
                        assert(workItem->getState() != TaskState::Executing);
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
                        assert(workItem->getState() != TaskState::Executing);
                        return;
                    }
                    continue; // retry wait on this item.
                }
                case TaskState::Executing: {
                    // Waiting for an item that's currently being executed.
                    FAIL_IF_WAITING_WILL_DEADLOCK(*workItem);
                    PUSH_WAITING_FOR;
                    if (executingWorkItem) {
                        --readyThreads;
                    }
                    // Block this thread.
                    workItem->future.wait();
                    if (executingWorkItem) {
                        ++readyThreads;
                    }
                    POP_WAITING_FOR;
                    assert(workItem->getState() != TaskState::Executing);
                    return;
                }
            }
        }
    }

    void pool::work() {
        threadOwningPool = this;
        while (true) {
            std::unique_lock unstartedLock(unstartedMutex);
            cv.wait(unstartedLock, [&] {
                return (!unstarted.empty() && workingThreads.load(std::memory_order::acquire) < targetParallelism)
                || stopping.load(std::memory_order::acquire);
            });

            if (stopping.load(std::memory_order::acquire) && unstarted.empty())
                return;

            ++workingThreads;
            // Take the first item that we can successfully mark as executing.
            auto item = std::find_if(unstarted.begin(), unstarted.end(),
                                     [](const std::shared_ptr<WorkItem>& wi) {
                                         return wi->trySetExecuting();
                                     });
            if (item == unstarted.end()) {
                // Failed to mark any item as executing.
                std::this_thread::yield();
                --workingThreads;
                continue;
            }
            // Copy the value because we're about to use it
            // after invalidating the iterator,
            // plus to increment the reference count.
            auto itemValue = *item; // NOLINT(*-unnecessary-copy-initialization)
            unstarted.erase(item);
            unstartedLock.unlock();
            itemValue->execute();
            --workingThreads;
        }
    }
}
