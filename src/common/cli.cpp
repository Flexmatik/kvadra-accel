#include "common/cli.h"

#include <stdexcept>
#include <string>

namespace accel {
namespace {

bool IsOption(const std::string& value) { return value.rfind("--", 0) == 0; }

std::string RequireValue(int& index, int argc, char** argv,
                         const std::string& option) {
    if (index + 1 >= argc || IsOption(argv[index + 1])) {
        throw std::runtime_error("missing value for " + option);
    }
    ++index;
    return argv[index];
}

bool ParseBool(const std::string& value) {
    if (value == "1" || value == "true" || value == "on" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "off" || value == "no") {
        return false;
    }
    throw std::runtime_error("invalid boolean value: " + value);
}

}  // namespace

AppConfig LoadConfigFromArgs(  // NOLINT
    int argc, char** argv, const std::filesystem::path& default_path) {
    std::filesystem::path config_path = default_path;
    int first_override = 1;
    if (argc > 1 && !IsOption(argv[1])) {
        config_path = argv[1];
        first_override = 2;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string kOption = argv[i];
        if (kOption == "--config") {
            config_path = RequireValue(i, argc, argv, kOption);
            first_override = i + 1;
        }
    }

    AppConfig config = LoadConfig(config_path);
    for (int i = first_override; i < argc; ++i) {
        const std::string kOption = argv[i];
        if (kOption == "--config") {
            RequireValue(i, argc, argv, kOption);
        } else if (kOption == "--host") {
            config.server_host = RequireValue(i, argc, argv, kOption);
        } else if (kOption == "--node-a-port") {
            config.node_a_port =
                std::stoi(RequireValue(i, argc, argv, kOption));
        } else if (kOption == "--node-b-port") {
            config.node_b_port =
                std::stoi(RequireValue(i, argc, argv, kOption));
        } else if (kOption == "--grpc-port") {
            config.grpc_port = std::stoi(RequireValue(i, argc, argv, kOption));
        } else if (kOption == "--api-key") {
            config.api_key = RequireValue(i, argc, argv, kOption);
        } else if (kOption == "--sensor-hz") {
            config.sensor_hz = std::stoi(RequireValue(i, argc, argv, kOption));
        } else if (kOption == "--reconnect-delay-ms") {
            config.reconnect_delay = std::chrono::milliseconds(
                std::stoi(RequireValue(i, argc, argv, kOption)));
        } else if (kOption == "--module-log") {
            config.module_log_path = RequireValue(i, argc, argv, kOption);
        } else if (kOption == "--max-samples") {
            config.max_samples =
                std::stoi(RequireValue(i, argc, argv, kOption));
        } else if (kOption == "--grpc-tls") {
            config.grpc_tls = ParseBool(RequireValue(i, argc, argv, kOption));
        } else if (kOption == "--grpc-mtls") {
            config.grpc_mutual_tls =
                ParseBool(RequireValue(i, argc, argv, kOption));
        } else if (kOption == "--tcp-tls") {
            config.tcp_tls = ParseBool(RequireValue(i, argc, argv, kOption));
        } else if (kOption == "--tcp-mtls") {
            config.tcp_mutual_tls =
                ParseBool(RequireValue(i, argc, argv, kOption));
        } else if (kOption == "--tls-target-name") {
            config.tls_target_name = RequireValue(i, argc, argv, kOption);
        } else if (kOption == "--ca-cert") {
            config.ca_cert_path = RequireValue(i, argc, argv, kOption);
        } else if (kOption == "--server-cert") {
            config.server_cert_path = RequireValue(i, argc, argv, kOption);
        } else if (kOption == "--server-key") {
            config.server_key_path = RequireValue(i, argc, argv, kOption);
        } else if (kOption == "--client-cert") {
            config.client_cert_path = RequireValue(i, argc, argv, kOption);
        } else if (kOption == "--client-key") {
            config.client_key_path = RequireValue(i, argc, argv, kOption);
        } else {
            throw std::runtime_error("unknown option: " + kOption);
        }
    }

    ValidateConfig(config);
    return config;
}

}  // namespace accel
