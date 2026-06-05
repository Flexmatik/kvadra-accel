#include "common/config.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace accel {
namespace {

template <typename T>
void ReadOptional(const nlohmann::json& json, const char* key, T& value) {
    if (json.contains(key)) {
        value = json.at(key).get<T>();
    }
}

}  // namespace

AppConfig LoadConfig(const std::filesystem::path& path) {
    AppConfig config;
    if (path.empty()) {
        ValidateConfig(config);
        return config;
    }

    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open config file: " + path.string());
    }

    const auto kJson = nlohmann::json::parse(input);
    ReadOptional(kJson, "server_host", config.server_host);
    ReadOptional(kJson, "node_a_port", config.node_a_port);
    ReadOptional(kJson, "node_b_port", config.node_b_port);
    ReadOptional(kJson, "grpc_port", config.grpc_port);
    ReadOptional(kJson, "api_key", config.api_key);
    ReadOptional(kJson, "sensor_hz", config.sensor_hz);
    ReadOptional(kJson, "duplicate_precision", config.duplicate_precision);
    ReadOptional(kJson, "max_samples", config.max_samples);
    ReadOptional(kJson, "grpc_tls", config.grpc_tls);
    ReadOptional(kJson, "grpc_mutual_tls", config.grpc_mutual_tls);
    ReadOptional(kJson, "tcp_tls", config.tcp_tls);
    ReadOptional(kJson, "tcp_mutual_tls", config.tcp_mutual_tls);
    ReadOptional(kJson, "tls_target_name", config.tls_target_name);

    if (kJson.contains("reconnect_delay_ms")) {
        config.reconnect_delay = std::chrono::milliseconds(
            kJson.at("reconnect_delay_ms").get<int>());
    }
    if (kJson.contains("module_log_path")) {
        config.module_log_path = kJson.at("module_log_path").get<std::string>();
    }
    if (kJson.contains("ca_cert_path")) {
        config.ca_cert_path = kJson.at("ca_cert_path").get<std::string>();
    }
    if (kJson.contains("server_cert_path")) {
        config.server_cert_path =
            kJson.at("server_cert_path").get<std::string>();
    }
    if (kJson.contains("server_key_path")) {
        config.server_key_path = kJson.at("server_key_path").get<std::string>();
    }
    if (kJson.contains("client_cert_path")) {
        config.client_cert_path =
            kJson.at("client_cert_path").get<std::string>();
    }
    if (kJson.contains("client_key_path")) {
        config.client_key_path = kJson.at("client_key_path").get<std::string>();
    }

    ValidateConfig(config);
    return config;
}

void ValidateConfig(const AppConfig& config) {
    if (config.node_a_port <= 0 || config.node_a_port > 65535 ||
        config.node_b_port <= 0 || config.node_b_port > 65535 ||
        config.grpc_port <= 0 || config.grpc_port > 65535) {
        throw std::runtime_error("ports must be in range 1..65535");
    }
    if (config.node_a_port == config.node_b_port ||
        config.node_a_port == config.grpc_port ||
        config.node_b_port == config.grpc_port) {
        throw std::runtime_error(
            "node_a_port, node_b_port, and grpc_port must be different");
    }
    if (config.api_key.empty()) {
        throw std::runtime_error("api_key must not be empty");
    }
    if (config.sensor_hz <= 0 || config.sensor_hz > 1000) {
        throw std::runtime_error("sensor_hz must be in range 1..1000");
    }
    if (config.reconnect_delay.count() < 0) {
        throw std::runtime_error("reconnect_delay_ms must be non-negative");
    }
    if (config.duplicate_precision < 0 || config.duplicate_precision > 9) {
        throw std::runtime_error("duplicate_precision must be in range 0..9");
    }
    if (config.max_samples < 0) {
        throw std::runtime_error("max_samples must be non-negative");
    }
    if (config.grpc_tls && config.tls_target_name.empty()) {
        throw std::runtime_error(
            "tls_target_name must not be empty when grpc_tls is enabled");
    }
    if (config.grpc_mutual_tls && !config.grpc_tls) {
        throw std::runtime_error("grpc_mutual_tls requires grpc_tls");
    }
    if (config.tcp_mutual_tls && !config.tcp_tls) {
        throw std::runtime_error("tcp_mutual_tls requires tcp_tls");
    }
}

}  // namespace accel
