#include "common/config.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
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

TEST_CASE(  // NOLINT
    "LoadConfig reads configurable sensor frequency and reconnect delay") {
    const std::filesystem::path kPath =
        std::filesystem::temp_directory_path() / "KvadraAccel_config_test.json";
    {
        std::ofstream output(kPath);
        output << R"({
            "server_host": "127.0.0.1",
            "node_a_port": 6101,
            "node_b_port": 6102,
            "grpc_port": 6103,
            "api_key": "test-key",
            "sensor_hz": 25,
            "reconnect_delay_ms": 250,
            "duplicate_precision": 4,
            "module_log_path": "tmp/module.log",
            "max_samples": 7,
            "grpc_tls": true,
            "grpc_mutual_tls": true,
            "tcp_tls": true,
            "tcp_mutual_tls": true,
            "tls_target_name": "localhost",
            "ca_cert_path": "certs/ca.crt",
            "server_cert_path": "certs/server.crt",
            "server_key_path": "certs/server.key",
            "client_cert_path": "certs/client.crt",
            "client_key_path": "certs/client.key"
        })";
    }

    const auto kConfig = accel::LoadConfig(kPath);

    REQUIRE(kConfig.server_host == "127.0.0.1");
    REQUIRE(kConfig.node_a_port == 6101);
    REQUIRE(kConfig.node_b_port == 6102);
    REQUIRE(kConfig.grpc_port == 6103);
    REQUIRE(kConfig.api_key == "test-key");
    REQUIRE(kConfig.sensor_hz == 25);
    REQUIRE(kConfig.reconnect_delay.count() == 250);
    REQUIRE(kConfig.duplicate_precision == 4);
    REQUIRE(kConfig.module_log_path == "tmp/module.log");
    REQUIRE(kConfig.max_samples == 7);
    REQUIRE(kConfig.grpc_tls);
    REQUIRE(kConfig.grpc_mutual_tls);
    REQUIRE(kConfig.tcp_tls);
    REQUIRE(kConfig.tcp_mutual_tls);
    REQUIRE(kConfig.tls_target_name == "localhost");
    REQUIRE(kConfig.ca_cert_path == "certs/ca.crt");
    REQUIRE(kConfig.server_cert_path == "certs/server.crt");
    REQUIRE(kConfig.server_key_path == "certs/server.key");
    REQUIRE(kConfig.client_cert_path == "certs/client.crt");
    REQUIRE(kConfig.client_key_path == "certs/client.key");

    std::filesystem::remove(kPath);
}

TEST_CASE("ValidateConfig rejects invalid sensor frequency") {
    accel::AppConfig config;
    config.sensor_hz = 0;

    REQUIRE_THROWS(accel::ValidateConfig(config));
}

TEST_CASE("ValidateConfig rejects duplicate ports") {
    accel::AppConfig config;
    config.node_a_port = config.node_b_port;

    RequireThrowsMessage(
        [&] { accel::ValidateConfig(config); },
        "node_a_port, node_b_port, and grpc_port must be different");
}

TEST_CASE("ValidateConfig rejects empty API key") {
    accel::AppConfig config;
    config.api_key.clear();

    RequireThrowsMessage([&] { accel::ValidateConfig(config); },
                         "api_key must not be empty");
}

TEST_CASE("ValidateConfig rejects invalid duplicate precision") {
    accel::AppConfig config;
    config.duplicate_precision = 10;

    RequireThrowsMessage([&] { accel::ValidateConfig(config); },
                         "duplicate_precision must be in range 0..9");
}

TEST_CASE("ValidateConfig rejects empty TLS target when TLS is enabled") {
    accel::AppConfig config;
    config.grpc_tls = true;
    config.tls_target_name.clear();

    RequireThrowsMessage(
        [&] { accel::ValidateConfig(config); },
        "tls_target_name must not be empty when grpc_tls is enabled");
}

TEST_CASE("ValidateConfig rejects mTLS without TLS") {
    accel::AppConfig config;
    config.grpc_tls = false;
    config.grpc_mutual_tls = true;

    REQUIRE_THROWS(accel::ValidateConfig(config));
}

TEST_CASE("ValidateConfig rejects TCP mTLS without TCP TLS") {
    accel::AppConfig config;
    config.tcp_tls = false;
    config.tcp_mutual_tls = true;

    REQUIRE_THROWS(accel::ValidateConfig(config));
}
