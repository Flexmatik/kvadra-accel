#include "common/cli.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace {

std::vector<char*> ToArgv(std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    return argv;
}

std::filesystem::path WriteMinimalConfig(const std::string& name) {
    const std::filesystem::path kPath =
        std::filesystem::temp_directory_path() / name;
    std::ofstream output(kPath);
    output << R"({
        "server_host": "127.0.0.1",
        "node_a_port": 6501,
        "node_b_port": 6502,
        "grpc_port": 6503,
        "api_key": "test-key"
    })";
    return kPath;
}

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

TEST_CASE("LoadConfigFromArgs applies CLI overrides") {
    const std::filesystem::path kPath =
        std::filesystem::temp_directory_path() / "KvadraAccel_cli_test.json";
    {
        std::ofstream output(kPath);
        output << R"({
            "server_host": "127.0.0.1",
            "node_a_port": 6201,
            "node_b_port": 6202,
            "grpc_port": 6203,
            "api_key": "from-file"
        })";
    }

    std::vector<std::string> args = {
        "KvadraAccel_server", kPath.string(), "--node-a-port", "6301",
        "--api-key",      "from-cli",     "--tcp-tls",     "true",
        "--tcp-mtls",     "true",
    };
    auto argv = ToArgv(args);

    const auto kConfig = accel::LoadConfigFromArgs(
        static_cast<int>(argv.size()), argv.data(), "config/local.json");

    REQUIRE(kConfig.node_a_port == 6301);
    REQUIRE(kConfig.api_key == "from-cli");
    REQUIRE(kConfig.tcp_tls);
    REQUIRE(kConfig.tcp_mutual_tls);

    std::filesystem::remove(kPath);
}

TEST_CASE("LoadConfigFromArgs accepts --config form") {
    const std::filesystem::path kPath = std::filesystem::temp_directory_path() /
                                        "KvadraAccel_cli_config_option_test.json";
    {
        std::ofstream output(kPath);
        output << R"({
            "server_host": "127.0.0.1",
            "node_a_port": 6401,
            "node_b_port": 6402,
            "grpc_port": 6403,
            "api_key": "config-option-key"
        })";
    }

    std::vector<std::string> args = {"KvadraAccel_node_a", "--config",
                                     kPath.string(), "--sensor-hz", "75"};
    auto argv = ToArgv(args);

    const auto kConfig = accel::LoadConfigFromArgs(
        static_cast<int>(argv.size()), argv.data(), "missing.json");

    REQUIRE(kConfig.api_key == "config-option-key");
    REQUIRE(kConfig.sensor_hz == 75);

    std::filesystem::remove(kPath);
}

TEST_CASE("LoadConfigFromArgs rejects unknown options") {
    const auto kPath =
        WriteMinimalConfig("KvadraAccel_cli_unknown_option_test.json");
    std::vector<std::string> args = {"KvadraAccel_server", kPath.string(),
                                     "--unknown"};
    auto argv = ToArgv(args);

    RequireThrowsMessage(
        [&] {
            accel::LoadConfigFromArgs(static_cast<int>(argv.size()),
                                      argv.data(), kPath);
        },
        "unknown option: --unknown");
    std::filesystem::remove(kPath);
}

TEST_CASE("LoadConfigFromArgs rejects missing option values") {
    const auto kPath =
        WriteMinimalConfig("KvadraAccel_cli_missing_value_test.json");
    std::vector<std::string> args = {"KvadraAccel_server", kPath.string(),
                                     "--api-key"};
    auto argv = ToArgv(args);

    RequireThrowsMessage(
        [&] {
            accel::LoadConfigFromArgs(static_cast<int>(argv.size()),
                                      argv.data(), kPath);
        },
        "missing value for --api-key");
    std::filesystem::remove(kPath);
}

TEST_CASE("LoadConfigFromArgs rejects invalid booleans") {
    const auto kPath =
        WriteMinimalConfig("KvadraAccel_cli_invalid_boolean_test.json");
    std::vector<std::string> args = {"KvadraAccel_server", kPath.string(),
                                     "--tcp-tls", "maybe"};
    auto argv = ToArgv(args);

    RequireThrowsMessage(
        [&] {
            accel::LoadConfigFromArgs(static_cast<int>(argv.size()),
                                      argv.data(), kPath);
        },
        "invalid boolean value: maybe");
    std::filesystem::remove(kPath);
}
