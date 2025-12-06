#include "TestUtils.h"
#include "WorkerPool.h"

using namespace WorkerPool;

TEST(Name, NoNameVoid) {
    Pool p(1);
    auto task = p.add([] {
    });
    EXPECT_TRUE(task.getName().empty());
    task.wait();
}

TEST(Name, NoNameNonVoid) {
    Pool p(1);
    auto task = p.add([] { return 5; });
    EXPECT_TRUE(task.getName().empty());
    task.wait();
}

TEST(Name, VoidName) {
    const std::string NAME = "Hello, World!";
    Pool p(1);
    auto task = p.add(NAME, [] {
    });
    EXPECT_EQ(NAME, task.getName());
    task.wait();
}

TEST(Name, NonVoidName) {
    const std::string NAME = "Hello, World!";
    Pool p(1);
    auto task = p.add(NAME, [] { return 5; });
    EXPECT_EQ(NAME, task.getName());
    task.wait();
}
