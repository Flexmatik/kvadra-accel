#include "tcp/level1.h"

#ifndef KVADRA_ACCEL_HAS_ASIO_TCP

#include <fstream>
#include <thread>

#include "common/json_protocol.h"
#include "common/logger.h"
#include "common/socket.h"

namespace accel {
namespace {

void SleepBeforeReconnect(const AppConfig& config) {
    Logger::Info("reconnecting in ", config.reconnect_delay.count(), " ms");
    std::this_thread::sleep_for(config.reconnect_delay);
}

void SendHello(TcpConnection& connection, const AppConfig& config,
               ClientRole role) {
    connection.WriteLine(SerializeHello({
        .version = kProtocolVersion,
        .role = role,
        .api_key = config.api_key,
    }));
}

bool CheckHello(TcpConnection& connection, const AppConfig& config,
                ClientRole expected_role) {
    const auto hello = ParseHello(connection.ReadLine());
    return IsAuthorized(hello, config.api_key, expected_role);
}

}  // namespace

ModuleLogWriter::ModuleLogWriter(const AppConfig& config)
    : path_(config.module_log_path) {
    if (path_.has_parent_path()) {
        std::filesystem::create_directories(path_.parent_path());
    }
}

void ModuleLogWriter::Write(const AccelModule& module) {
    std::ofstream output(path_, std::ios::app);
    if (!output) {
        throw std::runtime_error("cannot open module log: " + path_.string());
    }
    output << module.timestamp << ' ' << module.module << '\n';
}

ServerApp::ServerApp(AppConfig config)
    : config_(std::move(config)),
      duplicate_filter_(config_.duplicate_precision) {}

void ServerApp::Run() {
    if (config_.tcp_tls) {
        throw std::runtime_error(
            "tcp_tls requires Boost.Asio runtime; install libboost-dev and "
            "build with KVADRA_ACCEL_BUILD_ASIO_TCP=ON");
    }
    Logger::Info("server starting on ports A=", config_.node_a_port,
                 " B=", config_.node_b_port);
    std::jthread node_a_thread([this] { AcceptNodeA(); });
    std::jthread node_b_thread([this] { AcceptNodeB(); });
}

void ServerApp::AcceptNodeA() {
    TcpListener listener(config_.node_a_port);
    while (true) {
        try {
            Logger::Info("waiting for node A");
            auto connection = listener.Accept();
            if (!CheckHello(connection, config_, ClientRole::NodeA)) {
                Logger::Error(
                    "node A rejected: invalid version, role, or api key");
                continue;
            }
            Logger::Info("node A connected");
            HandleNodeA(std::move(connection));
        } catch (const std::exception& error) {
            Logger::Error("node A connection error: ", error.what());
        }
    }
}

void ServerApp::AcceptNodeB() {
    TcpListener listener(config_.node_b_port);
    while (true) {
        try {
            Logger::Info("waiting for node B");
            auto connection = listener.Accept();
            if (!CheckHello(connection, config_, ClientRole::NodeB)) {
                Logger::Error(
                    "node B rejected: invalid version, role, or api key");
                continue;
            }
            Logger::Info("node B connected");
            HandleNodeB(std::move(connection));
        } catch (const std::exception& error) {
            Logger::Error("node B connection error: ", error.what());
        }
    }
}

void ServerApp::HandleNodeA(TcpConnection connection) {
    std::jthread writer([this, &connection](std::stop_token stop) {
        try {
            while (!stop.stop_requested()) {
                auto module =
                    modules_to_a_.PopFor(std::chrono::milliseconds(200));
                if (module.has_value()) {
                    connection.WriteLine(SerializeModule(module.value()));
                }
            }
        } catch (const std::exception& error) {
            Logger::Error("node A writer stopped: ", error.what());
        }
    });

    while (true) {
        const auto packet = ParsePacket(connection.ReadLine());
        if (duplicate_filter_.Accept(packet)) {
            packets_to_b_.Push(packet);
        } else {
            Logger::Info("duplicate packet dropped at timestamp ",
                         packet.timestamp);
        }
    }
}

void ServerApp::HandleNodeB(TcpConnection connection) {
    std::jthread writer([this, &connection](std::stop_token stop) {
        try {
            while (!stop.stop_requested()) {
                auto packet =
                    packets_to_b_.PopFor(std::chrono::milliseconds(200));
                if (packet.has_value()) {
                    connection.WriteLine(SerializePacket(packet.value()));
                }
            }
        } catch (const std::exception& error) {
            Logger::Error("node B writer stopped: ", error.what());
        }
    });

    while (true) {
        modules_to_a_.Push(ParseModule(connection.ReadLine()));
    }
}

NodeAApp::NodeAApp(AppConfig config) : config_(std::move(config)) {}

void NodeAApp::Run() {
    if (config_.tcp_tls) {
        throw std::runtime_error(
            "tcp_tls requires Boost.Asio runtime; install libboost-dev and "
            "build with KVADRA_ACCEL_BUILD_ASIO_TCP=ON");
    }
    SensorEmulator sensor;
    ModuleLogWriter log_writer(config_);
    const auto interval =
        std::chrono::microseconds(1'000'000 / config_.sensor_hz);

    while (true) {
        try {
            Logger::Info("node A connecting to ", config_.server_host, ":",
                         config_.node_a_port);
            auto connection = TcpConnection::Connect(config_.server_host,
                                                     config_.node_a_port);
            SendHello(connection, config_, ClientRole::NodeA);
            Logger::Info("node A connected");

            int sent = 0;
            while (config_.max_samples == 0 || sent < config_.max_samples) {
                const auto packet = sensor.Next();
                connection.WriteLine(SerializePacket(packet));
                ++sent;

                while (auto line = connection.ReadLineIfAvailable(0)) {
                    log_writer.Write(ParseModule(line.value()));
                }

                std::this_thread::sleep_for(interval);
            }
            return;
        } catch (const std::exception& error) {
            Logger::Error("node A transport error: ", error.what());
            SleepBeforeReconnect(config_);
        }
    }
}

NodeBApp::NodeBApp(AppConfig config) : config_(std::move(config)) {}

void NodeBApp::Run() {
    if (config_.tcp_tls) {
        throw std::runtime_error(
            "tcp_tls requires Boost.Asio runtime; install libboost-dev and "
            "build with KVADRA_ACCEL_BUILD_ASIO_TCP=ON");
    }
    while (true) {
        try {
            Logger::Info("node B connecting to ", config_.server_host, ":",
                         config_.node_b_port);
            auto connection = TcpConnection::Connect(config_.server_host,
                                                     config_.node_b_port);
            SendHello(connection, config_, ClientRole::NodeB);
            Logger::Info("node B connected");

            while (true) {
                const auto packet = ParsePacket(connection.ReadLine());
                connection.WriteLine(
                    SerializeModule(ModuleCalculator::Compute(packet)));
            }
        } catch (const std::exception& error) {
            Logger::Error("node B transport error: ", error.what());
            SleepBeforeReconnect(config_);
        }
    }
}

}  // namespace accel
#endif
