# WorkerPool

WorkerPool is a thread pool. It aims to be easy to use.

## Features
 - Configurable degree of parallelism
 - Simple API; `worker_pool::task` resembles `std::shared_future`
 - Tasks can be void or have copy-constructible output
 - Tasks can create and await other tasks
 - The pool can await multiple tasks at once
 - Supports timeouts for waits
 - Supports cancellation for unstarted tasks
 - Supports tasks that throw exceptions
 - Not global, allows multiple pools in one process
 - You can provide your own thread factory
 - Tasks can have names

## Usage

### Simple example
```c++
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
using namespace worker_pool;

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
using namespace worker_pool;

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
using namespace worker_pool;

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

### Throwing exceptions from tasks
Exceptions with tasks work like you'd expect from `std::future`.
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
The pool destructor waits for all tasks to finish,
so if this takes too long for you,
you need to give your tasks some signal to stop,
or consider simply making them take less time unconditionally.
That said, WorkerPool does offer some ways to stop work.

#### Shutting down a pool
Calling `shutDown` on a pool prevents new work from being added to it.
`shutDown(false)` is called automatically in the pool destructor.
```c++
pool p;
p.add([]{});
p.shutDown();

// This will throw an exception because the pool has been shut down
p.add([]{}); 
```

By default, a pool eventually does all the work ever added to it,
even after `shutDown` is called.
By passing `true` to `shutDown`,
you can tell the pool to cancel all unstarted work.
```c++
// Create a pool that can do 1 thing at a time.
pool pool(1, 0, false);

auto t1 = pool.add([]{ sleep(2); });
auto t2 = pool.add([]{ sleep(2); });
auto t3 = pool.add([]{ sleep(2); });
sleep(1); // One of the three tasks will begin during this time.
pool.shutDown(true); // This will cancel the other tasks.
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
    
    t.get(); // Returns 123. might not return immediately.
}
```

### Custom thread factory
You can provide your own threads for use by the pool.
To do so, provide a callable that takes the pool's `std::function<void()>` callback
and returns a `std::thread` that executes it.
The pool will execute this callback whenever it wants to create a thread.
```c++
#include <pthread.h>
#include <cstdio>
...
using namespace worker_pool;

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

### Named tasks
Tasks can be given names, which you might find useful for debugging or other purposes.

You assign a task's name when you create it,
and you can retrieve the task's name with `task::get_name()`.
Outside of this, the library does not use a task's name for any purpose.
```c++
using namespace worker_pool;

pool pool;
auto task = pool.add("apples", []{});

// Will print "Created task apples"
std::cout << "Created task " << task.get_name() << '\n';
```

### More examples

The unit tests in `test/src/` are good sources of further examples.

## Further reading

A discussion of the design of the task waiting features can be found [on my blog](https://geoff.space/2025/12/intelligent-waiting-in-a-thread-pool/).