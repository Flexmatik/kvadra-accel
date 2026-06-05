#include "common/blocking_queue.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

TEST_CASE("BlockingQueue pops values in FIFO order") {
    accel::BlockingQueue<int> queue;

    queue.Push(1);
    queue.Push(2);

    REQUIRE(queue.PopFor(std::chrono::milliseconds(1)).value() == 1);
    REQUIRE(queue.PopFor(std::chrono::milliseconds(1)).value() == 2);
}

TEST_CASE("BlockingQueue returns nullopt on timeout") {
    accel::BlockingQueue<int> queue;

    const auto kStarted = std::chrono::steady_clock::now();
    const auto kValue = queue.PopFor(std::chrono::milliseconds(10));
    const auto kElapsed = std::chrono::steady_clock::now() - kStarted;

    REQUIRE_FALSE(kValue.has_value());
    REQUIRE(kElapsed >= std::chrono::milliseconds(8));
}

TEST_CASE("BlockingQueue close wakes waiting consumer") {
    accel::BlockingQueue<int> queue;
    bool consumer_woke = false;

    std::jthread consumer([&] {
        const auto kValue = queue.PopFor(std::chrono::seconds(5));
        consumer_woke = !kValue.has_value();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.Close();
    consumer.join();

    REQUIRE(consumer_woke);
}

TEST_CASE("BlockingQueue ignores pushes after close") {
    accel::BlockingQueue<int> queue;

    queue.Close();
    queue.Push(10);

    REQUIRE_FALSE(queue.PopFor(std::chrono::milliseconds(1)).has_value());
}
