#include "TestUtils.h"
#include "worker-pool/worker-pool.h"

using namespace worker_pool;

TEST(Name, NoNameVoid) {
    pool p(1);
    auto task = p.add("", [] {
    });
    EXPECT_TRUE(task.get_name().empty());
}

TEST(Name, NoNameNonVoid) {
    pool p(1);
    auto task = p.add("", [] { return 5; });
    EXPECT_TRUE(task.get_name().empty());
}

TEST(Name, VoidManual) {
    const std::string NAME = "Hello, World!";
    pool p(1);
    auto task = p.add(NAME, [] {
    });
    EXPECT_EQ(NAME, task.get_name());
}

TEST(Name, VoidAuto) {
    pool p(1);
    auto t1 = p.add([] {
    });
    auto t2 = p.add([] {
    });
    EXPECT_FALSE(t1.get_name().empty());
    EXPECT_FALSE(t1.get_name().empty());
    EXPECT_NE(t1.get_name(), t2.get_name());
}

TEST(Name, NonVoidManual) {
    const std::string NAME = "Hello, World!";
    pool p(1);
    auto task = p.add(NAME, [] { return 5; });
    EXPECT_EQ(NAME, task.get_name());
}

TEST(Name, NonVoidAuto) {
    pool p(1);
    auto t1 = p.add([] { return 5; });
    auto t2 = p.add([] { return 6; });
    EXPECT_FALSE(t1.get_name().empty());
    EXPECT_FALSE(t1.get_name().empty());
    EXPECT_NE(t1.get_name(), t2.get_name());
}

TEST(Name, PoolAuto) {
    // Assert that every overload gets a generated name,
    // and that they're all distinct.
    pool p1;
    pool p2;
    pool p3(1);
    pool p4(1);
    pool p5(1, 1);
    pool p6(1, 1);
    pool p7(1, 1, false);
    pool p8(1, 1, false);
    pool p9(1, 1, [](const std::function<void()> cb) { return std::thread(cb); });
    pool p10(1, 1, [](const std::function<void()> cb) { return std::thread(cb); });

    EXPECT_FALSE(p1.get_name().empty());
    EXPECT_FALSE(p2.get_name().empty());
    EXPECT_FALSE(p3.get_name().empty());
    EXPECT_FALSE(p4.get_name().empty());
    EXPECT_FALSE(p5.get_name().empty());
    EXPECT_FALSE(p6.get_name().empty());
    EXPECT_FALSE(p7.get_name().empty());
    EXPECT_FALSE(p8.get_name().empty());
    EXPECT_FALSE(p9.get_name().empty());
    EXPECT_FALSE(p10.get_name().empty());
    std::set<std::string> distinctNames;
    distinctNames.insert(p1.get_name());
    distinctNames.insert(p2.get_name());
    distinctNames.insert(p3.get_name());
    distinctNames.insert(p4.get_name());
    distinctNames.insert(p5.get_name());
    distinctNames.insert(p6.get_name());
    distinctNames.insert(p7.get_name());
    distinctNames.insert(p8.get_name());
    distinctNames.insert(p9.get_name());
    distinctNames.insert(p10.get_name());
    EXPECT_EQ(10, distinctNames.size());
}

TEST(Name, PoolPrefix) {
    const std::string POOL_NAME = "hello";
    pool p(POOL_NAME, 1);
    EXPECT_EQ(POOL_NAME, p.get_name());
    auto task = p.add([] {
    });
    const auto taskName = task.get_name();
    ASSERT_FALSE(taskName.empty());
    ASSERT_TRUE(taskName.starts_with(POOL_NAME));
}
