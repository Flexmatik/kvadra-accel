#include "grpc/proto_conversion.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("AccelPacket converts to and from protobuf") {
    const accel::AccelPacket kPacket{
        .timestamp = 123, .x = 1.25, .y = 2.5, .z = -3.75};

    const auto kConverted = accel::FromProto(accel::ToProto(kPacket));

    REQUIRE(kConverted.version == accel::kProtocolVersion);
    REQUIRE(kConverted.timestamp == 123);
    REQUIRE_THAT(kConverted.x, Catch::Matchers::WithinAbs(1.25, 0.000001));
    REQUIRE_THAT(kConverted.y, Catch::Matchers::WithinAbs(2.5, 0.000001));
    REQUIRE_THAT(kConverted.z, Catch::Matchers::WithinAbs(-3.75, 0.000001));
}

TEST_CASE("AccelModule converts to and from protobuf") {
    const accel::AccelModule kModule{.timestamp = 123, .module = 9.81};

    const auto kConverted = accel::FromProto(accel::ToProto(kModule));

    REQUIRE(kConverted.version == accel::kProtocolVersion);
    REQUIRE(kConverted.timestamp == 123);
    REQUIRE_THAT(kConverted.module, Catch::Matchers::WithinAbs(9.81, 0.00001));
}
