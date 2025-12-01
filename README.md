# worker-pool

`worker-pool` is a thread pool. It aims to be easy to use.

## Features
 - Configurable degree of parallelism 
 - Tasks can be void or have copy-constructible output
 - Tasks can create and await other tasks
 - You can provide your own thread factory
 - Can await a collection of tasks
 - Not global, allows multiple pools in one process

## Usage

### Simple example
```c++
// Create a pool that will run up to 2 threads in parallel.
WorkerPool pool(2);

// Add some tasks to the pool.
// The pool will begin executing them as soon as possible.
Task t1 = pool.add([]{ puts("I'm a task"); });
Task t2 = pool.add([]{ puts("I'm another task"); });

// Ensure that the tasks are finished before proceeding.
t1.wait();
t2.wait();
```

### Result example
```c++
WorkerPool pool(1);

constexpr auto X = 2;
constexpr auto Y = 5;
Task sumTask = pool.add([X, Y]{ return X + Y; });

sumTask.wait(); // Calling wait is not needed here, since getResult will wait.
const auto sum = sumTask.getResult();
printf("%d + %d = %d\n", X, Y, sum); // Prints 2 + 5 = 7
```

### Different ways to add a task
```c++

constexpr auto X = 2;
constexpr auto Y = 5;

// Using labmda with capture
Task sumTask = pool.add([X, Y]{ return X + Y; });

// Using extra arguments
Task sumTask = pool.add([](int x, int y) { return x + y; }, X, Y);

// Using named callback
static void add(int x, int y) { return x + y; }
Task sumTask = pool.add(add, X, Y);
```

### Waiting for multiple tasks
```c++
WorkerPool pool(1);
std::vector<WorkerPool::Task<void>> tasks;
task.emplace_back(pool.add([]{ puts("I'm a task"); }));
task.emplace_back(pool.add([]{ puts("I'm another task"); }));

// Wait for all tasks to be finished.
pool.waitAll(tasks);

// Equivalently,
pool.waitAll(tasks.data(), tasks.size());
```
Compared with sequentially calling `Task::wait` on each of a set of tasks,
`WorkerPool::waitAll` is more convenient,
and in some scenarios, also more performant.

### Custom thread factory
```c++
#include <pthread.h>
#include <cstdio>
...
WorkerPool pool(4, 4,
                 [&](const std::function<void()>& callback) {
                     return std::thread([=] {
                        const auto status = pthread_setschedprio(pthread_self(), 20);
                        if (status != 0) {
                            errno = status;
                            perror("pthread_setschedprio");
                        }
                        callback();
                     });
                 });
```
All threads created by the above pool will have their priority set to 20.

### More examples

The unit tests in `test/src/` are good sources of further examples.

## Discussion: How waiting works

When you call `wait` on a task, you block the current thread until that task is done.
If `wait` were to be called from a pool thread, the pool's effective parallelism would drop.
WorkerPool uses two strategies to mitigate this.

First, when `wait` is called on a task that has not yet started,
WorkerPool will use the calling thread to execute the task.
This makes the "waiting" thread do something useful instead of just blocking.

Second, when `wait` is called on a task that is already in progress,
the calling thread has no choice but to simply wait for it to finish.
However, if the calling thread belongs to the pool,
WorkerPool may add an extra worker thread
in order to sustain its target degree of parallelism.

The maximum number of extra threads to create for this purpose
can be set in the WorkerPool constructor.
If you do not specify a value,
the default is to use up to the same number of extra threads
as the regular target parallelism.
Consider raising the number of extra threads if you notice
the pool isn't hitting its target degree of parallelism.
This is more likely to happen when
 - the pool simultaneously has a large number of tasks
   (more tasks than threads); and
 - a large number of the tasks wait for other pool tasks

The downside of using a huge value for the extra threads limit
is that although extra threads are created lazily,
you could run out of memory if a huge number of them is actually created.

The pool's parallelism is always limited to its target value.
Therefore, when waiting threads become ready again and extra threads are no longer needed,
the extra threads will not be used unless needed again to offset waiters.

This example demonstrates one advantage of using the pool to wait:
```c++
WorkerPool pool(2);
Task outer = pool.add([&] {
    auto inner1 = pool.add([] { sleep(1); });
    auto inner2 = pool.add([] { sleep(1); });
    inner1.wait();
    inner2.wait();
});
outer.wait();
```
Task `outer` (and the entire operation) should take less than 2 seconds,
even though at first glance we have
 - 2 tasks that take 1 second each,
 - 3 nontrivial tasks, and
 - a pool that can only do 2 things at once

Execution of the above code is nondeterministic, but it could proceed like this:
1. The main thread creates a pool with 2 worker threads and up to 2 extra threads.
2. The main thread adds a task `outer` to the pool and waits for its completion.
3. Worker 1 begins Task `outer`.
4. Task `outer` adds Task `inner1` to the pool.
5. Worker 2 begins Task `inner1`.
6. Task `outer` adds Task `inner2` to the pool.
There are no worker threads ready to begin this task.
7. Task `outer` calls `inner1.wait()`.
This will block Worker 1, so the pool creates an extra thread, Worker 3, to sustain parallelism.
While Worker 1 blocks, Worker 3 begins work on Task `inner2`.
8. Worker 2 finishes Task `inner1`.
9. Worker 3 finishes Task `inner2`.
10. Worker 1 (in Task `outer`) returns from `inner1.wait()`.
11. Task `outer` calls `inner2.wait()`.
Task `inner2` has already been completed by Worker 3
while `outer` was waiting for `inner1`, this `wait` returns immediately.

The above execution trace shows how extra threads can be helpful
to keep work happening when a worker thread is waiting.
Let's now consider the other mitigation against blocking in pool threads.
```c++
WorkerPool pool(1, 0); // 0 extra threads
Task outer = pool.add([&] {
    auto inner = pool.add([] { sleep(1); });
    inner.wait();
});
outer.wait();
```
It looks like this code might deadlock, because
 - the pool can only do 1 thing at once (and has no extra threads to help)
 - we have 2 tasks, the first of which can't complete until the second does

But deadlock is avoided, and the code finishes in about 1 second.
It could happen like this:
1. The main thread creates a pool with 1 worker thread and no allowed extra threads.
2. The main thread adds a task `outer` to the pool and waits for its completion.
3. Worker 1 begins Task `outer`.
4. Task `outer` adds Task `inner` to the pool.
There are no worker threads ready to begin this task.
5. Task `outer` calls `inner.wait()`.
Task `inner` has not been started,
so the waiting thread (Worker 1) executes `inner` synchronously.
6. Worker 1 finishes executing `inner`.
7. Worker 1 returns from `inner.wait()` and Task `outer` is done.
