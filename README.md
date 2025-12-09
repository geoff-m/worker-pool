# WorkerPool

WorkerPool is a thread pool. It aims to be easy to use.

## Features
 - Configurable degree of parallelism
 - Tasks can be void or have copy-constructible output
 - Tasks can create and await other tasks
 - The pool can await multiple tasks at once
 - Supports timeouts for waits
 - Simple API; `worker_pool::task` resembles `std::shared_future`
 - You can provide your own thread factory
 - Not global, allows multiple pools in one process
 - Tasks can have names

## Usage

### Simple example
```c++
using namespace worker_pool;

// Create a pool that will run up to 2 threads in parallel.
pool pool(2);

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
using namespace worker_pool;

pool pool(1);

constexpr auto X = 2;
constexpr auto Y = 5;
Task sumTask = pool.add([X, Y]{ return X + Y; });
 
sumTask.wait(); // Calling wait is not needed here, since getResult will wait.
const auto sum = sumTask.getResult();
printf("%d + %d = %d\n", X, Y, sum); // Prints 2 + 5 = 7
```

### Different ways to add a task
```c++
using namespace worker_pool;

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
using namespace worker_pool;

pool pool(1);
std::vector<Task<void>> tasks;
tasks.emplace_back(pool.add([]{ puts("I'm a task"); }));
tasks.emplace_back(pool.add([]{ puts("I'm another task"); }));

// Wait for all tasks to be finished.
pool.wait_all(tasks);

// Equivalently,
pool.wait_all(tasks.begin(), tasks.end());

// Equivalently,
pool.wait_all(tasks.data(), tasks.size());
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
    puts("Task timed out");

// Timed wait for multiple tasks
std::vector<Task<void>> tasks;
tasks.emplace_back(pool.add(/* ... */));
tasks.emplace_back(pool.add(/* ... */));

if (pool.wait_all_for(tasks, std::chrono::seconds(1)))
    puts("All tasks are done");
else
    puts("Not all tasks are done");
```

### Custom thread factory
You can provide your own thread for use by the pool.
To do so, provide a callable that takes the pool's `std::function<void()>` callback
and returns a `std::thread` that executes it.
```c++
#include <pthread.h>
#include <cstdio>
...
using namespace worker_pool;

Pool pool(4, 4,
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
and you can retrieve the task's name with `Task::getName()`.
Outside of this, the library does not use a task's name for any purpose.
```c++
using namespace worker_pool;

Pool pool(2);
auto task = pool.add("apples", []{});

// Will print "Created task apples"
std::cout << "Created task " << task.getName() << '\n';
```

### More examples

The unit tests in `test/src/` are good sources of further examples.

## Further reading

A discussion of the design of the task waiting features can be found [on my blog](https://geoff.space/2025/12/intelligent-waiting-in-a-thread-pool/).