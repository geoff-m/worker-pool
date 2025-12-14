#include "TestUtils.h"
#include "worker-pool/worker-pool.h"
#include <stdexcept>

using namespace worker_pool;

void assertThrows(const std::function<void()>& callback, const char* expectedExceptionString) {
    try {
        callback();
    } catch (const std::exception& ex) {
        EXPECT_STREQ(ex.what(), expectedExceptionString);
        return;
    } catch (...) {
        FAIL() << "Caught unexpected type";
    }
    FAIL() << "Didn't catch expected exception";
}

template<typename ThrownValueType>
void assertThrows(const std::function<void()>& callback, ThrownValueType expectedThrownValue) {
    try {
        callback();
    } catch (const ThrownValueType& ex) {
        EXPECT_EQ(expectedThrownValue, ex);
        return;
    } catch (...) {
        FAIL() << "Caught unexpected type";
    }
    FAIL() << "Didn't catch expected value";
}

TEST(Exceptions, RuntimeExceptionFromVoidTask) {
    pool pool;
    constexpr auto EXPECTED_STRING = "Hello";
    auto task = pool.add([] {
        throw std::runtime_error(EXPECTED_STRING);
    });
    task.wait();
    assertThrows([&] { task.get(); }, EXPECTED_STRING);
    assertThrows([&] { task.get(); }, EXPECTED_STRING);
}

TEST(Exceptions, IntFromVoidTask) {
    pool pool;
    constexpr int EXPECTED_INT = 7865;
    auto task = pool.add([] {
        throw EXPECTED_INT;
    });
    task.wait();
    assertThrows([&] { task.get(); }, EXPECTED_INT);
    assertThrows([&] { task.get(); }, EXPECTED_INT);
}

TEST(Exceptions, RuntimeExceptionFromIntTask) {
    pool pool(1, 0, false);
    constexpr auto EXPECTED_STRING = "Hello";
    auto task = pool.add([] {
        throw std::runtime_error(EXPECTED_STRING);
        return 123;
    });
    sleepMs(1000);
    task.wait();
    assertThrows([&] { (void)task.get(); }, EXPECTED_STRING);
    assertThrows([&] { (void)task.get(); }, EXPECTED_STRING);
}

TEST(Exceptions, IntFromIntTask) {
    pool pool;
    constexpr int EXPECTED_INT = 7865;
    auto task = pool.add([] {
        throw EXPECTED_INT;
        return 123;
    });
    task.wait();
    assertThrows([&] { (void)task.get(); }, EXPECTED_INT);
    assertThrows([&] { (void)task.get(); }, EXPECTED_INT);
}
