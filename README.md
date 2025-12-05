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
using namespace WorkerPool;

// Create a pool that will run up to 2 threads in parallel.
Pool pool(2);

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
using namespace WorkerPool;

Pool pool(1);

constexpr auto X = 2;
constexpr auto Y = 5;
Task sumTask = pool.add([X, Y]{ return X + Y; });

sumTask.wait(); // Calling wait is not needed here, since getResult will wait.
const auto sum = sumTask.getResult();
printf("%d + %d = %d\n", X, Y, sum); // Prints 2 + 5 = 7
```

### Different ways to add a task
```c++
using namespace WorkerPool;

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
using namespace WorkerPool;

Pool pool(1);
std::vector<Pool::Task<void>> tasks;
task.emplace_back(pool.add([]{ puts("I'm a task"); }));
task.emplace_back(pool.add([]{ puts("I'm another task"); }));

// Wait for all tasks to be finished.
pool.waitAll(tasks);

// Equivalently,
pool.waitAll(tasks.data(), tasks.size());
```
Compared with sequentially calling `Task::wait` on each of a set of tasks,
`Pool::waitAll` is more convenient,
and in some scenarios, also more performant.

### Custom thread factory
```c++
#include <pthread.h>
#include <cstdio>
...
using namespace WorkerPool;
Pool pool(4, 4,
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

## Further reading

A discussion of some of the task waiting features can be found [on my blog](https://geoff.space/2025/12/notes-on-a-thread-pool/).