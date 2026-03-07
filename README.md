# WorkerPool

WorkerPool is a thread pool. It aims to be easy to use.

## Features

- Configurable degree of parallelism
- Simple API; `worker_pool::task` resembles `std::shared_future`
- No dependencies other than C++20
- Tasks can be void or have copy-constructible output
- Tasks can create and await other tasks
- The pool can await multiple tasks at once
- Optionally operates as a bounded blocking queue
- All blocking operations support timeouts
- Supports cancellation for unstarted tasks
- Supports tasks that throw exceptions
- Not global, allows multiple pools in one process
- You can provide your own thread factory
- Useful for debugging: Can detect deadlocks arising from cyclic waits
- Useful for debugging: Pools and tasks have names

## Usage

### Simple example

```c++
#include "worker-pool/worker-pool.h"
using namespace worker_pool;

// Create a thread pool with an automatic number of threads.
pool pool;

// Add some tasks to the pool.
// The pool will begin executing them as soon as possible but in no particular order.
task t1 = pool.add([]{ puts("I'm a task"); });
task t2 = pool.add([]{ puts("I'm another task"); });

// Ensure that the tasks are finished before proceeding.
t1.wait();
t2.wait();
```

### Result example

```c++
// Create a pool that will do up to 8 things in parallel.
pool pool(8);

constexpr auto X = 2;
constexpr auto Y = 5;
task sumTask = pool.add([X, Y]{ return X + Y; });
 
sumTask.wait(); // Calling wait is not needed here, since task::get will wait.
const auto sum = sumTask.get();

printf("%d + %d = %d\n", X, Y, sum); // Prints 2 + 5 = 7
```

### Different ways to add a task

```c++
constexpr auto X = 2;
constexpr auto Y = 5;

// Using labmda with capture
task sumTask = pool.add([X, Y]{ return X + Y; });

// Using extra arguments
task sumTask = pool.add([](int x, int y) { return x + y; }, X, Y);

// Using named callback function
static void add(int x, int y) { return x + y; }
task sumTask = pool.add(add, X, Y);
```

### Waiting for multiple tasks

```c++
pool pool;
std::vector<task<void>> tasks;
tasks.emplace_back(pool.add([]{ puts("I'm a task"); }));
tasks.emplace_back(pool.add([]{ puts("I'm another task"); }));

// Wait for all tasks to be finished.
pool::wait_all(tasks);

// Equivalently,
pool::wait_all(tasks.begin(), tasks.end());

// Equivalently,
pool::wait_all(tasks.data(), tasks.size());
```

Compared with sequentially calling `task::wait` on each of a set of tasks,
`pool::wait_all` is more convenient, and in some scenarios, also more performant.
Further discussion of the task waiting features can be found
[on my blog](https://geoff.space/2025/12/intelligent-waiting-in-a-thread-pool/).

### Timed waiting

`wait` and `wait_all` have counterparts that can time out:

- `wait_for`
- `wait_until`
- `wait_all_for`
- `wait_all_until`

All of these return a `bool`:

- `true` indicates the awaited operation(s) finished
- `false` indicates the timeout elapsed

```c++
// Timed wait for single task
auto task = pool.add(/* ... */);
if (task.wait_for(std::chrono::seconds(1))) 
    puts("Task is done");
else
    puts("Task is not done");

// Timed wait for multiple tasks
std::vector<task<void>> tasks;
tasks.emplace_back(pool.add(/* ... */));
tasks.emplace_back(pool.add(/* ... */));

if (pool::wait_all_for(tasks, std::chrono::seconds(1)))
    puts("All tasks are done");
else
    puts("Not all tasks are done");
```

### Task states
A task is always in one of four states.
You can query a task's state any time using `get_state()`, or using
the functions `is_unstarted()`, `is_executing()`, `is_done()`, `is_canceled()`.

| TaskState     | Meaning                                       | Behavior of wait()   | Behavior of get()                                                    |
|---------------|-----------------------------------------------|----------------------|----------------------------------------------------------------------|
| **Unstarted** | Initial state. The task is queued in a pool.  | Blocks.              | Blocks.                                                              |
| **Executing** | The task is being executed.                   | Blocks.              | Blocks.                                                              |
| **Done**      | The task's execution is complete.             | Returns immediately. | Immediately returns the task's result (or throws if the task threw). |
| **Canceled**  | The task was canceled before execution began. | Returns immediately. | Immediately throws an exception indicating the task was canceled.    |

### Throwing exceptions from tasks

Exceptions with tasks work like you'd expect from `std::shared_future`.
If a task throws an exception, the exception will be stored.
Calls to `wait` will return immediately.
Calling `get` will rethrow the exception.

```c++
pool pool;
task<int> t = pool.add([] {
    if (rand() < RAND_MAX / 2)
        throw std::runtime_error("oops");
    return 42;
});

t.get(); // Will either return 42 or (re)throw.
```

### Stopping work

Once your tasks start running, only you can interrupt them
(using `std::stop_token`, etc.).
The pool destructor waits for all tasks to finish.
However, WorkerPool does offer some ways to stop work.

#### Shutting down a pool

Calling `shut_down` on a pool prevents new work from being added to it.
`shut_down(false)` is called automatically in the pool destructor.

```c++
pool p;
p.add([]{});
p.shut_down();

// This will throw an exception because the pool has been shut down.
p.add([]{}); 
```

By default, a pool eventually does all the work ever added to it,
even after `shut_down` is called.
By passing `true` to `shut_down`,
you can tell the pool to cancel all unstarted work.

```c++
// Create a pool that can do 1 thing at a time.
pool pool(1, 0, false);

auto t1 = pool.add([]{ sleep(2); });
auto t2 = pool.add([]{ sleep(2); });
auto t3 = pool.add([]{ sleep(2); });
sleep(1); // One of the three tasks will likely begin.
pool.shut_down(true); // This will cancel the other two unstarted tasks.
```

#### Canceling a specific task

Call `try_cancel` to cancel an unstarted task.
`try_cancel` will fail if the task has already been started.
Awaiting a canceled task will immediately finish.
Getting the result from a canceled task will immediately throw an exception.

```c++
pool pool;
auto t = pool.add([]{ return 123; });
if (t.try_cancel()) {
    // Succesfully canceled task.
    // It is guaranteed not to start.
    
    t.get(); // Throws an exception immediately.
} else {
    // Failed to cancel task.
    // It has already been started, finished, or canceled.
    
    t.get(); // Returns 123. Might not return immediately.
}
```

### Pool creation options
The following table lists all parameters that can be set when creating a pool.
All of them can be omitted, and good defaults will be used instead.

| **Name**                  | **Type**                                    | **Meaning**                                                                                                                | **Default**                    |
|---------------------------|---------------------------------------------|----------------------------------------------------------------------------------------------------------------------------|--------------------------------|
| `name`                    | `std::string`                               | Name of the pool returned by `pool::get_name()`                                                                            | "pool#" where # is unique      |
| `targetParallelism`       | `unsigned int`                              | Number of tasks the pool will try to do in parallel                                                                        | Detected hardware thread count |
| `extraThreads`            | `unsigned int`                              | Maximum number of threads beyond `targetParallelism` that the pool will use as backup if some threads get blocked by waits | Value of `targetParallelism`   |
| `queueSize`               | `size_t`                                    | Maximum number of tasks to hold in the queue                                                                               | `0` (= unbounded)              |
| `fullQueuePolicy`         | `FullQueuePolicy`                           | Controls what should happen when attempting to add a new task when the queue is at its size limit                          | `FullQueuePolicy::Block`       |
| `threadFactory`           | `function<thread(const function<void()>&)>` | Callback to create a thread that runs the given function                                                                   | The obvious implementation     |
| `allowWorkOffPoolThreads` | `bool`                                      | Whether calling `task::wait` on a non-pool thread is allowed to do pool work while waiting                                 | `true`                         |

A builder class is provided, which can facilitate a more readable alternative to call sites with large argument lists.
```c++
pool_bulider pb;

// Call whatever setters you want on the builder.
pb.set_target_parallelism(4);
pb.set_name("my pool");

// Finally, construct the pool using the options you set on the builder.
pool pool = pb.build();
```

### Backpressure

Some applications may need to create a large number of pool tasks,
or some tasks may be expensive to initialize.
For example, in the following code, a large number of tasks are created.
```c++
pool pool;
std::vector<task<void>> tasks;
for (int i = 0; i < 10000000; ++i) {
    tasks.emplace_back(pool.add(/* ... */));
}
pool::wait_all(tasks);
```
For convenience, we offer a few alternatives for situations like the above.

#### Limiting task creation by bounding the queue
```c++
pool_builder builder;
builder.set_target_parallelism(8);
builder.set_queue_size(16);
//builder.set_full_queue_policy(FullQueuePolicy::Block); // Implicit default
auto pool = builder.build();
for (int i = 0; i < 10000000; ++i) {
    pool.add(/* ... */);
}
```
The above example bounds the pool's task queue size to 16.
Because the policy is the default of `Block`,
calls to `pool.add` wait until there is space for the new task in the queue.

There are three options for `FullQueuePolicy`:
- `Block` (default): When attempting to add a new task to a full queue, block until space is available.
 - `DropOld`: When attempting to add a new task to a full queue,
cancel the oldest task and enqueue the new one.
 - `DropNew`: When attempting to add a new task to a full queue,
cancel the new task and don't enqueue it.

#### Nonblocking task creation with a bounded queue
If you want to add a task only if the queue is not full, call `try_add`.
If the queue is full when you call `try_add`, the full queue policy does not apply.
Instead, the method immediately returns false and does not create a task.
```c++
task<int> maybeTask; // For now, an invalid task.
if (pool.try_add(maybeTask, [] { return 123; })) {
    // Successfully added task to pool (queue was not full).
    
    /* ... */ = maybeTask.get(); // returns 123
} else {
    // Did not successfully add task to pool (queue was full).
    
    // Error: not a valid task
    /* ... */ = maybeTask.get();
}
```

#### Waiting for task completion without a task object
There are ways to avoid having to store a large number of tasks in order to wait for them.
The least error-prone alternative to `wait_all` is to destroy the pool.
```c++
{
    pool pool;
    for (int i = 0; i < 10000000; ++i) {
        pool.add(/* ... */);
    }
} // All pending tasks will be awaited upon destruction of pool.
```

Another way is to call `await_idle_pool()` (or a timed version).
The function `await_idle_thread()` (or a timed version) can be employed to similar ends.
```c++
  pool pool;
  for (int i = 0; i < 10000000; ++i) {
      pool.await_idle_thread();
      pool.add(/* ... */);
  }
pool.await_idle_pool();
```
In the above example, the pool's degree of parallelism limits the number of tasks
that will exist simultaneously.

However, since `await_idle_thread()` and `await_idle_pool()` present only an ephemeral view of the pool's state,
applications that rely on them must carefully avoid TOCTOU bugs.
It is preferred as less error-prone to wait for specific tasks by calling a `wait*` function,
and the preferred way to wait for an entire pool to be finished is to destroy the pool.

### Custom thread factory

You can provide your own threads for use by the pool.
To do so, provide a callable that takes the pool's `std::function<void()>` callback
and returns a `std::thread` that executes it.
In the pool's constructor, this callback will be used to create all the threads
the pool will ever use.

```c++
#include <pthread.h>
#include <cstdio>

pool pool(4, 4,
  [&](const std::function<void()>& callback) {
    return std::thread([=] {
      // Custom logic to set up this pool thread
      const auto status = pthread_setschedprio(pthread_self(), 20);
      if (status != 0) {
        errno = status;
        perror("pthread_setschedprio");
      }
      
      // Execute the thread pool's code
      callback();
    });
 });
```

All threads created by the above pool will have their priority set to 20.

### Names for pools and tasks

Pools and tasks can be given names, which you might find useful for debugging or other purposes.

For both types of objects, you assign their names when you create them,
and you can retrieve the name with `get_name()`.

```c++
pool pool("my pool")
auto task = pool.add("some task", []{});
```

If you do not provide a name, one will be generated automatically.
Generated task names will be prefixed with the owning pool's name.

```c++
pool pool;
auto task = pool.add([]{});
std::cout << "Pool name: " << pool.get_name() << '\n';
std::cout << "Task name: " << task.get_name() << '\n';
```
As an example, the above code might print
```
Pool name: pool0
Task name: pool0_task0
```

### Deadlock detection

When tasks wait for each other (or themselves) in a cycle,
this results in a deadlock.
WorkerPool can help you catch this situation when it's about to happen.
It is enabled and controlled by two CMake options:

- `WORKER_POOL_DEADLOCK_DETECTION`, which can be
    - Off (Default)
    - On

- `WORKER_POOL_DEADLOCK_DETECTION_ACTION`, which can be
    - Throw (Default)
    - Terminate

When enabled, calling an untimed pool wait function in a way that would
deadlock will cause the selected action to be taken instead.

```c++
pool pool(2, 0, false);
task<void>* pt2 = nullptr;
auto t1 = pool.add("t1", [&] {
    /* Not shown: Wait until pt2 is nonnull */

    pt2->wait();
});

auto t2 = pool.add("t2", [&] {
    sleep(1);
    t1.wait();
});
pt2 = &t2;

try {
    // One of these will throw.
    // (Which one throws is nondeterministic in this example.)
    t1.get();
    t2.get();
} catch (const std::exception& ex) {
    printf("Error: %s\n", ex.what());
}
```

With the aforementioned CMake options set, the above code will print something like

```
Error: The requested wait would deadlock: t1 would wait for itself via t2 -> t1.
```

That message means t2 was waiting for t1
at the time when t1 tried to start waiting for t2.

If you set `WORKER_POOL_DEADLOCK_DETECTION_ACTION` = `Terminate`,
then `wait` will call `std::terminate` instead of throwing.

Note that deadlock detection applies only to WorkerPool's untimed wait functions
and does not attempt to find or prevent other kinds of deadlocks that may exist in your program.

Further discussion of what this feature is and how it works can be found
[on my blog](https://geoff.space/2025/12/detecting-deadlocks-in-a-thread-pool/).

### More examples

The unit tests in `test/src/` are good sources of further examples.
