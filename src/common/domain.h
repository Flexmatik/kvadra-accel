#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <random>

namespace accel {

inline constexpr int kProtocolVersion = 1;

struct AccelPacket {
    int version{kProtocolVersion};
    std::int64_t timestamp{0};
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct AccelModule {
    int version{kProtocolVersion};
    std::int64_t timestamp{0};
    double module{0.0};
};

std::int64_t NowMillis();

class DuplicateFilter {
public:
    explicit DuplicateFilter(int precision_digits = 3);

    bool IsDuplicate(const AccelPacket& packet) const;
    bool Accept(const AccelPacket& packet);

private:
    struct RoundedVector {
        long long x{0};
        long long y{0};
        long long z{0};

        bool operator==(const RoundedVector& other) const = default;
    };

    RoundedVector Round(const AccelPacket& packet) const;

    int scale_{1000};
    std::optional<RoundedVector> previous_;
};

class ModuleCalculator {
public:
    [[nodiscard]] static AccelModule Compute(const AccelPacket& packet);
};

class SensorEmulator {
public:
    explicit SensorEmulator(unsigned seed = 6767);

    [[nodiscard]] AccelPacket Next();

private:
    std::uint64_t sample_index_{0};
    std::mt19937 rng_;
    std::normal_distribution<double> noise_{0.0, 0.015};
};

}  // namespace accel
