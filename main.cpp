#include <cstdio>
#include "WorkerPool.h"

int add(int x, int y) {
    return x + y;
}

int main() {
    WorkerPool pool(2);
    auto intFuture = pool.add<int>([] { return 123; });
    auto stringFuture = pool.add<std::string>([] { return "hello"; });
    auto binaryFuture = pool.add<int>(&add, 2, 3);
    auto binaryLambdaFuture = pool.add<int>([](int x, int y) { return x + y; },
        2, 3);

    // std::this_thread::sleep_for(std::chrono::seconds(3));
    // printf("shutting down pool\n");
    // pool.shutDown();

    intFuture.wait();
    printf("int == %d\n", intFuture.get());
    stringFuture.wait();
    printf("string == %s\n", stringFuture.get().c_str());
    binaryFuture.wait();
    printf("binary == %d\n", binaryFuture.get());
    binaryLambdaFuture.wait();
    printf("binaryLambda == %d\n", binaryLambdaFuture.get());

    return 0;
}
