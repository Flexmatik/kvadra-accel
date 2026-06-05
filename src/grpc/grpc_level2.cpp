#include "grpc/grpc_level2.h"

#include <grpc/grpc_security_constants.h>
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "accelerometer.grpc.pb.h"
#include "common/blocking_queue.h"
#include "common/logger.h"
#include "grpc/proto_conversion.h"
#include "tcp/level1.h"

namespace accel {
namespace {

constexpr std::string_view kApiKeyMetadata = "x-api-key";

bool HasValidApiKey(grpc::ServerContext* context,
                    const std::string& expected_key) {
    const auto& metadata = context->client_metadata();
    const auto kFound = metadata.find(std::string(kApiKeyMetadata));
    return kFound != metadata.end() &&
           std::string(kFound->second.data(), kFound->second.length()) ==
               expected_key;
}

void AddApiKey(grpc::ClientContext& context, const std::string& api_key) {
    context.AddMetadata(std::string(kApiKeyMetadata), api_key);
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open TLS file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::shared_ptr<grpc::ServerCredentials> CreateServerCredentials(
    const AppConfig& config) {
    if (!config.grpc_tls) {
        return grpc::InsecureServerCredentials();
    }

    grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair{
        ReadTextFile(config.server_key_path),
        ReadTextFile(config.server_cert_path),
    };

    grpc::SslServerCredentialsOptions options;
    options.pem_root_certs = ReadTextFile(config.ca_cert_path);
    options.pem_key_cert_pairs.push_back(std::move(key_cert_pair));
    options.client_certificate_request =
        config.grpc_mutual_tls
            ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
            : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;

    return grpc::SslServerCredentials(options);
}

std::shared_ptr<grpc::Channel> CreateInsecureChannel(const AppConfig& config) {
    return grpc::CreateChannel(
        config.server_host + ":" + std::to_string(config.grpc_port),
        grpc::InsecureChannelCredentials());
}

std::shared_ptr<grpc::Channel> CreateChannel(const AppConfig& config) {
    if (!config.grpc_tls) {
        return CreateInsecureChannel(config);
    }

    grpc::SslCredentialsOptions options;
    options.pem_root_certs = ReadTextFile(config.ca_cert_path);
    if (config.grpc_mutual_tls) {
        options.pem_private_key = ReadTextFile(config.client_key_path);
        options.pem_cert_chain = ReadTextFile(config.client_cert_path);
    }

    grpc::ChannelArguments channel_args;
    channel_args.SetSslTargetNameOverride(config.tls_target_name);

    return grpc::CreateCustomChannel(
        config.server_host + ":" + std::to_string(config.grpc_port),
        grpc::SslCredentials(options), channel_args);
}

class AccelerometerServiceImpl final
    : public accel::v1::AccelerometerService::Service {
public:
    explicit AccelerometerServiceImpl(AppConfig config)
        : config_(std::move(config)),
          duplicate_filter_(config_.duplicate_precision) {}

    grpc::Status StreamAccelData(
        grpc::ServerContext* context,
        grpc::ServerReaderWriter<accel::v1::AccelModule,
                                 accel::v1::AccelPacket>* stream) override {
        if (!HasValidApiKey(context, config_.api_key)) {
            Logger::Error("gRPC node A rejected: invalid api key");
            return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                                "invalid api key");
        }

        Logger::Info("gRPC node A stream connected");
        std::jthread writer([this, stream](std::stop_token stop) {
            try {
                while (!stop.stop_requested()) {
                    auto module =
                        modules_to_a_.PopFor(std::chrono::milliseconds(200));
                    if (module.has_value()) {
                        stream->Write(ToProto(module.value()));
                    }
                }
            } catch (const std::exception& error) {
                Logger::Error("gRPC node A writer stopped: ", error.what());
            }
        });

        accel::v1::AccelPacket proto_packet;
        while (stream->Read(&proto_packet)) {
            auto packet = FromProto(proto_packet);
            if (packet.version != kProtocolVersion) {
                Logger::Error("gRPC node A sent unsupported protocol version ",
                              packet.version);
                continue;
            }
            if (duplicate_filter_.Accept(packet)) {
                packets_to_b_.Push(packet);
            } else {
                Logger::Info("gRPC duplicate packet dropped at timestamp ",
                             packet.timestamp);
            }
        }

        Logger::Info("gRPC node A stream disconnected");
        return grpc::Status::OK;
    }

    grpc::Status ProcessAccelData(
        grpc::ServerContext* context,
        grpc::ServerReaderWriter<accel::v1::AccelPacket,
                                 accel::v1::AccelModule>* stream) override {
        if (!HasValidApiKey(context, config_.api_key)) {
            Logger::Error("gRPC node B rejected: invalid api key");
            return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                                "invalid api key");
        }

        Logger::Info("gRPC node B stream connected");
        std::jthread writer([this, stream](std::stop_token stop) {
            try {
                while (!stop.stop_requested()) {
                    auto packet =
                        packets_to_b_.PopFor(std::chrono::milliseconds(200));
                    if (packet.has_value()) {
                        stream->Write(ToProto(packet.value()));
                    }
                }
            } catch (const std::exception& error) {
                Logger::Error("gRPC node B writer stopped: ", error.what());
            }
        });

        accel::v1::AccelModule proto_module;
        while (stream->Read(&proto_module)) {
            auto module = FromProto(proto_module);
            if (module.version == kProtocolVersion) {
                modules_to_a_.Push(module);
            }
        }

        Logger::Info("gRPC node B stream disconnected");
        return grpc::Status::OK;
    }

private:
    AppConfig config_;
    DuplicateFilter duplicate_filter_;
    BlockingQueue<AccelPacket> packets_to_b_;
    BlockingQueue<AccelModule> modules_to_a_;
};

}  // namespace

GrpcServerApp::GrpcServerApp(AppConfig config) : config_(std::move(config)) {}

void GrpcServerApp::Run() {
    const std::string kAddress = "0.0.0.0:" + std::to_string(config_.grpc_port);
    AccelerometerServiceImpl service(config_);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(kAddress, CreateServerCredentials(config_));
    builder.RegisterService(&service);

    auto server = builder.BuildAndStart();
    if (!server) {
        throw std::runtime_error("failed to start gRPC server on " + kAddress);
    }

    Logger::Info("gRPC server listening on ", kAddress);
    server->Wait();
}

GrpcNodeAApp::GrpcNodeAApp(AppConfig config) : config_(std::move(config)) {}

void GrpcNodeAApp::Run() {
    SensorEmulator sensor;
    ModuleLogWriter log_writer(config_);
    const auto kInterval =
        std::chrono::microseconds(1'000'000 / config_.sensor_hz);

    while (true) {
        Logger::Info("gRPC node A connecting to ", config_.server_host, ":",
                     config_.grpc_port);
        auto stub =
            accel::v1::AccelerometerService::NewStub(CreateChannel(config_));
        grpc::ClientContext context;
        AddApiKey(context, config_.api_key);

        auto stream = stub->StreamAccelData(&context);
        std::jthread reader([&stream, &log_writer] {
            accel::v1::AccelModule proto_module;
            while (stream->Read(&proto_module)) {
                log_writer.Write(FromProto(proto_module));
            }
        });

        int sent = 0;
        while (config_.max_samples == 0 || sent < config_.max_samples) {
            if (!stream->Write(ToProto(sensor.Next()))) {
                break;
            }
            ++sent;
            std::this_thread::sleep_for(kInterval);
        }

        stream->WritesDone();
        const auto kStatus = stream->Finish();
        if (kStatus.ok() && config_.max_samples != 0) {
            return;
        }

        Logger::Error("gRPC node A stream ended: ", kStatus.error_message());
        std::this_thread::sleep_for(config_.reconnect_delay);
    }
}

GrpcNodeBApp::GrpcNodeBApp(AppConfig config) : config_(std::move(config)) {}

void GrpcNodeBApp::Run() {
    while (true) {
        Logger::Info("gRPC node B connecting to ", config_.server_host, ":",
                     config_.grpc_port);
        auto stub =
            accel::v1::AccelerometerService::NewStub(CreateChannel(config_));
        grpc::ClientContext context;
        AddApiKey(context, config_.api_key);

        auto stream = stub->ProcessAccelData(&context);
        accel::v1::AccelPacket proto_packet;
        while (stream->Read(&proto_packet)) {
            stream->Write(
                ToProto(ModuleCalculator::Compute(FromProto(proto_packet))));
        }

        stream->WritesDone();
        const auto kStatus = stream->Finish();
        Logger::Error("gRPC node B stream ended: ", kStatus.error_message());
        std::this_thread::sleep_for(config_.reconnect_delay);
    }
}

}  // namespace accel