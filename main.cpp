#include <cstdio>
#include "WorkerPool.h"

int add(int x, int y) {
    return x + y;
}

int main() {
    WorkerPool pool(2);
    auto intFuture = pool.add([] { return 123; });
    auto stringFuture = pool.add([] { return "hello"; });
    auto binaryFuture = pool.add(&add, 2, 3);
    auto binaryLambdaFuture = pool.add([](int x, int y) { return x + y; },
                                       2, 3);
    int voidSideEffect = 0;
    auto voidFuture = pool.add([&voidSideEffect] { voidSideEffect = 123; });
    int binaryVoidSideEffect = 0;
    auto binaryVoidFuture = pool.add([&binaryVoidSideEffect](int x, int y) {
        binaryVoidSideEffect = x + y;
    }, 2, 3);

    intFuture.wait();
    printf("int == %d\n", intFuture.get());
    stringFuture.wait();
    printf("string == %s\n", stringFuture.get());
    binaryFuture.wait();
    printf("binary == %d\n", binaryFuture.get());
    binaryLambdaFuture.wait();
    printf("binaryLambda == %d\n", binaryLambdaFuture.get());
    voidFuture.wait();
    printf("voidSideEffect == %d\n", voidSideEffect);
    binaryVoidFuture.wait();
    printf("binaryVoidSideEffect == %d\n", binaryVoidSideEffect);

    return 0;
}
