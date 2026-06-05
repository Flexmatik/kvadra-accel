#include "common/domain.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>

TEST_CASE("ModuleCalculator computes acceleration vector length") {
    const accel::AccelPacket kPacket{
        .timestamp = 10, .x = 3.0, .y = 4.0, .z = 12.0};

    const auto kModule = accel::ModuleCalculator::Compute(kPacket);

    REQUIRE(kModule.timestamp == 10);
    REQUIRE_THAT(kModule.module, Catch::Matchers::WithinAbs(13.0, 0.000001));
}

TEST_CASE("DuplicateFilter drops only consecutive rounded duplicates") {
    accel::DuplicateFilter filter(3);

    REQUIRE(filter.Accept({.x = 1.0001, .y = 2.0001, .z = 3.0001}));
    REQUIRE_FALSE(filter.Accept({.x = 1.0002, .y = 2.0002, .z = 3.0002}));
    REQUIRE(filter.Accept({.x = 1.002, .y = 2.0, .z = 3.0}));
    REQUIRE(filter.Accept({.x = 1.0001, .y = 2.0001, .z = 3.0001}));
}

TEST_CASE("DuplicateFilter handles configurable precision") {
    accel::DuplicateFilter whole_number_filter(0);

    REQUIRE(whole_number_filter.Accept({.x = 1.2, .y = 2.2, .z = 3.2}));
    REQUIRE_FALSE(whole_number_filter.Accept({.x = 1.3, .y = 2.3, .z = 3.3}));
    REQUIRE(whole_number_filter.Accept({.x = 1.6, .y = 2.2, .z = 3.2}));
}

TEST_CASE("ModuleCalculator preserves protocol version") {
    const accel::AccelPacket kPacket{
        .version = 7, .timestamp = 44, .x = 0.0, .y = 0.0, .z = 0.0};

    const auto kModule = accel::ModuleCalculator::Compute(kPacket);

    REQUIRE(kModule.version == 7);
    REQUIRE(kModule.timestamp == 44);
    REQUIRE_THAT(kModule.module, Catch::Matchers::WithinAbs(0.0, 0.000001));
}

TEST_CASE("NowMillis uses Unix epoch milliseconds") {
    const auto kBefore =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    const auto kTimestamp = accel::NowMillis();

    const auto kAfter = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

    REQUIRE(kTimestamp >= kBefore);
    REQUIRE(kTimestamp <= kAfter);
}

TEST_CASE("SensorEmulator produces versioned packets") {
    accel::SensorEmulator emulator;

    const auto kPacket = emulator.Next();

    REQUIRE(kPacket.version == accel::kProtocolVersion);
    REQUIRE(kPacket.timestamp > 0);
}

TEST_CASE("SensorEmulator is deterministic for a fixed seed") {
    accel::SensorEmulator first(123);
    accel::SensorEmulator second(123);

    const auto kFirstPacket = first.Next();
    const auto kSecondPacket = second.Next();

    REQUIRE_THAT(kFirstPacket.x,
                 Catch::Matchers::WithinAbs(kSecondPacket.x, 0.000001));
    REQUIRE_THAT(kFirstPacket.y,
                 Catch::Matchers::WithinAbs(kSecondPacket.y, 0.000001));
    REQUIRE_THAT(kFirstPacket.z,
                 Catch::Matchers::WithinAbs(kSecondPacket.z, 0.000001));
}
