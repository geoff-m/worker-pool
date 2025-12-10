#include "TestUtils.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;

TEST(Name, NoNameVoid) {
    pool p(1);
    auto task = p.add([] {
    });
    EXPECT_TRUE(task.get_name().empty());
    task.wait();
}

TEST(Name, NoNameNonVoid) {
    pool p(1);
    auto task = p.add([] { return 5; });
    EXPECT_TRUE(task.get_name().empty());
    task.wait();
}

TEST(Name, VoidName) {
    const std::string NAME = "Hello, World!";
    pool p(1);
    auto task = p.add(NAME, [] {
    });
    EXPECT_EQ(NAME, task.get_name());
    task.wait();
}

TEST(Name, NonVoidName) {
    const std::string NAME = "Hello, World!";
    pool p(1);
    auto task = p.add(NAME, [] { return 5; });
    EXPECT_EQ(NAME, task.get_name());
    task.wait();
}
