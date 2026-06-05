#include "common/json_protocol.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <functional>

namespace {

void RequireThrowsMessage(const std::function<void()>& action,
                          const std::string& expected) {
    try {
        action();
        FAIL("expected exception");
    } catch (const std::exception& error) {
        REQUIRE(error.what() == expected);
    }
}

}  // namespace

TEST_CASE("hello messages round-trip and validate API key") {
    const accel::HelloMessage kHello{
        .version = accel::kProtocolVersion,
        .role = accel::ClientRole::NodeB,
        .api_key = "secret",
    };

    const auto kParsed = accel::ParseHello(accel::SerializeHello(kHello));

    REQUIRE(kParsed.version == accel::kProtocolVersion);
    REQUIRE(kParsed.role == accel::ClientRole::NodeB);
    REQUIRE(accel::IsAuthorized(kParsed, "secret", accel::ClientRole::NodeB));
    REQUIRE_FALSE(
        accel::IsAuthorized(kParsed, "bad", accel::ClientRole::NodeB));
    REQUIRE_FALSE(
        accel::IsAuthorized(kParsed, "secret", accel::ClientRole::NodeA));
}

TEST_CASE("accel packets round-trip through newline JSON payload") {
    const accel::AccelPacket kPacket{
        .timestamp = 42, .x = 0.1, .y = 9.8, .z = -0.2};

    const auto kParsed = accel::ParsePacket(accel::SerializePacket(kPacket));

    REQUIRE(kParsed.version == accel::kProtocolVersion);
    REQUIRE(kParsed.timestamp == 42);
    REQUIRE_THAT(kParsed.x, Catch::Matchers::WithinAbs(0.1, 0.000001));
    REQUIRE_THAT(kParsed.y, Catch::Matchers::WithinAbs(9.8, 0.000001));
    REQUIRE_THAT(kParsed.z, Catch::Matchers::WithinAbs(-0.2, 0.000001));
}

TEST_CASE("module messages round-trip through newline JSON payload") {
    const accel::AccelModule kModule{.timestamp = 42, .module = 9.81};

    const auto kParsed = accel::ParseModule(accel::SerializeModule(kModule));

    REQUIRE(kParsed.version == accel::kProtocolVersion);
    REQUIRE(kParsed.timestamp == 42);
    REQUIRE_THAT(kParsed.module, Catch::Matchers::WithinAbs(9.81, 0.000001));
}

TEST_CASE("ParseRole rejects unknown roles") {
    RequireThrowsMessage([] { accel::ParseRole("admin"); },
                         "unknown client role: admin");
}

TEST_CASE("ParseHello rejects wrong message type") {
    RequireThrowsMessage(
        [] {
            accel::ParseHello(
                R"({"type":"accel","version":1,"role":"node_a","api_key":"secret"})");
        },
        "expected hello message");
}

TEST_CASE("ParsePacket rejects wrong message type") {
    RequireThrowsMessage(
        [] {
            accel::ParsePacket(
                R"({"type":"module","version":1,"timestamp":1,"module":9.8})");
        },
        "expected accel message");
}

TEST_CASE("ParseModule rejects malformed JSON") {
    REQUIRE_THROWS(accel::ParseModule("{not-json"));
}

TEST_CASE("authorization rejects unsupported protocol version") {
    const accel::HelloMessage kHello{
        .version = accel::kProtocolVersion + 1,
        .role = accel::ClientRole::NodeA,
        .api_key = "secret",
    };

    REQUIRE_FALSE(
        accel::IsAuthorized(kHello, "secret", accel::ClientRole::NodeA));
}
