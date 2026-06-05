#include "common/domain.h"

#include <cmath>

namespace accel {

std::int64_t NowMillis() {
    const auto kNow = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               kNow.time_since_epoch())
        .count();
}

DuplicateFilter::DuplicateFilter(int precision_digits) {
    scale_ = 1;
    for (int i = 0; i < precision_digits; ++i) {
        scale_ *= 10;
    }
}

bool DuplicateFilter::IsDuplicate(const AccelPacket& packet) const {
    return previous_.has_value() && previous_.value() == Round(packet);
}

bool DuplicateFilter::Accept(const AccelPacket& packet) {
    const auto kRounded = Round(packet);
    if (previous_.has_value() && previous_.value() == kRounded) {
        return false;
    }
    previous_ = kRounded;
    return true;
}

DuplicateFilter::RoundedVector DuplicateFilter::Round(
    const AccelPacket& packet) const {
    return {
        .x = std::llround(packet.x * static_cast<double>(scale_)),
        .y = std::llround(packet.y * static_cast<double>(scale_)),
        .z = std::llround(packet.z * static_cast<double>(scale_)),
    };
}

AccelModule ModuleCalculator::Compute(const AccelPacket& packet) {
    return {
        .version = packet.version,
        .timestamp = packet.timestamp,
        .module = std::sqrt((packet.x * packet.x) + (packet.y * packet.y) +
                            (packet.z * packet.z)),
    };
}

SensorEmulator::SensorEmulator(unsigned seed) : rng_(seed) {}

AccelPacket SensorEmulator::Next() {
    const double kTime = static_cast<double>(sample_index_) / 50.0;
    ++sample_index_;

    return {
        .version = kProtocolVersion,
        .timestamp = NowMillis(),
        .x = (std::sin(kTime) * 0.2) + noise_(rng_),
        .y = 9.80665 + (std::cos(kTime * 0.5) * 0.08) + noise_(rng_),
        .z = (std::sin(kTime * 0.25) * 0.12) + noise_(rng_),
    };
}

}  // namespace accel
