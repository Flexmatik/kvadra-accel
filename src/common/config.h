#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace accel {

struct AppConfig {
    std::string server_host{"127.0.0.1"};
    int node_a_port{5001};
    int node_b_port{5002};
    int grpc_port{50051};
    std::string api_key{"change-me"};
    int sensor_hz{50};
    std::chrono::milliseconds reconnect_delay{1000};
    int duplicate_precision{3};
    std::filesystem::path module_log_path{"accel/module.log"};
    int max_samples{0};
    bool grpc_tls{false};
    bool grpc_mutual_tls{false};
    bool tcp_tls{false};
    bool tcp_mutual_tls{false};
    std::string tls_target_name{"localhost"};
    std::filesystem::path ca_cert_path{"certs/ca.crt"};
    std::filesystem::path server_cert_path{"certs/server.crt"};
    std::filesystem::path server_key_path{"certs/server.key"};
    std::filesystem::path client_cert_path{"certs/client.crt"};
    std::filesystem::path client_key_path{"certs/client.key"};
};

AppConfig LoadConfig(const std::filesystem::path& path);
void ValidateConfig(const AppConfig& config);

}  // namespace accel
