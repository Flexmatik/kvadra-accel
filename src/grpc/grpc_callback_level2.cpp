#include <grpc/grpc_security_constants.h>
#include <grpcpp/grpcpp.h>

#include <condition_variable>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "accelerometer.grpc.pb.h"
#include "common/logger.h"
#include "grpc/grpc_level2.h"
#include "grpc/proto_conversion.h"
#include "tcp/level1.h"

namespace accel {
namespace {

constexpr std::string_view kApiKeyMetadata = "x-api-key";

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open TLS file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void AddApiKey(grpc::ClientContext& context, const std::string& api_key) {
    context.AddMetadata(std::string(kApiKeyMetadata), api_key);
}

bool HasValidApiKey(grpc::CallbackServerContext* context,
                    const std::string& expected_key) {
    const auto& metadata = context->client_metadata();
    const auto kFound = metadata.find(std::string(kApiKeyMetadata));
    return kFound != metadata.end() &&
           std::string(kFound->second.data(), kFound->second.length()) ==
               expected_key;
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

std::shared_ptr<grpc::Channel> CreateChannel(const AppConfig& config) {
    if (!config.grpc_tls) {
        return grpc::CreateChannel(
            config.server_host + ":" + std::to_string(config.grpc_port),
            grpc::InsecureChannelCredentials());
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

template <typename ReadT, typename WriteT>
class QueueingServerReactor : public grpc::ServerBidiReactor<ReadT, WriteT> {
public:
    void Send(WriteT message) {
        std::lock_guard lock(mutex_);
        write_queue_.push_back(std::move(message));
        TryStartWriteLocked();
    }

protected:
    void OnWriteDone(bool ok) override {
        std::lock_guard lock(mutex_);
        writing_ = false;
        if (!ok) {
            this->Finish(grpc::Status::OK);
            return;
        }
        TryStartWriteLocked();
    }

private:
    void TryStartWriteLocked() {
        if (writing_ || write_queue_.empty()) {
            return;
        }
        current_write_ = std::move(write_queue_.front());
        write_queue_.pop_front();
        writing_ = true;
        this->StartWrite(&current_write_);
    }

    std::mutex mutex_;
    std::deque<WriteT> write_queue_;
    WriteT current_write_;
    bool writing_{false};
};

class NodeAReactor;
class NodeBReactor;

struct CallbackRelayState {
    explicit CallbackRelayState(int duplicate_precision)
        : duplicate_filter(duplicate_precision) {}

    std::mutex mutex;
    DuplicateFilter duplicate_filter;
    NodeAReactor* node_a{nullptr};
    NodeBReactor* node_b{nullptr};
};

class NodeAReactor final
    : public QueueingServerReactor<accel::v1::AccelPacket,
                                   accel::v1::AccelModule> {
public:
    explicit NodeAReactor(CallbackRelayState& state) : state_(state) {
        {
            std::lock_guard lock(state_.mutex);
            state_.node_a = this;
        }
        StartRead(&read_);
    }

    void SendModule(const AccelModule& module) { Send(ToProto(module)); }

private:
    void OnReadDone(bool ok) override;

    void OnDone() override {
        {
            std::lock_guard lock(state_.mutex);
            if (state_.node_a == this) {
                state_.node_a = nullptr;
            }
        }
        delete this;
    }

    CallbackRelayState& state_;
    accel::v1::AccelPacket read_;
};

class NodeBReactor final
    : public QueueingServerReactor<accel::v1::AccelModule,
                                   accel::v1::AccelPacket> {
public:
    explicit NodeBReactor(CallbackRelayState& state) : state_(state) {
        {
            std::lock_guard lock(state_.mutex);
            state_.node_b = this;
        }
        StartRead(&read_);
    }

    void SendPacket(const AccelPacket& packet) { Send(ToProto(packet)); }

private:
    void OnReadDone(bool ok) override {
        if (!ok) {
            Finish(grpc::Status::OK);
            return;
        }

        NodeAReactor* node_a = nullptr;
        auto module = FromProto(read_);
        {
            std::lock_guard lock(state_.mutex);
            if (module.version == kProtocolVersion) {
                node_a = state_.node_a;
            }
        }
        if (node_a != nullptr) {
            node_a->SendModule(module);
        }
        StartRead(&read_);
    }

    void OnDone() override {
        {
            std::lock_guard lock(state_.mutex);
            if (state_.node_b == this) {
                state_.node_b = nullptr;
            }
        }
        delete this;
    }

    CallbackRelayState& state_;
    accel::v1::AccelModule read_;
};

void NodeAReactor::OnReadDone(bool ok) {
    if (!ok) {
        Finish(grpc::Status::OK);
        return;
    }

    NodeBReactor* node_b = nullptr;
    auto packet = FromProto(read_);
    {
        std::lock_guard lock(state_.mutex);
        if (packet.version == kProtocolVersion &&
            state_.duplicate_filter.Accept(packet)) {
            node_b = state_.node_b;
        }
    }
    if (node_b != nullptr) {
        node_b->SendPacket(packet);
    }
    StartRead(&read_);
}

class UnauthorizedPacketReactor final
    : public grpc::ServerBidiReactor<accel::v1::AccelPacket,
                                     accel::v1::AccelModule> {
public:
    UnauthorizedPacketReactor() {
        Finish(
            grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "invalid api key"));
    }

    void OnDone() override { delete this; }
};

class UnauthorizedModuleReactor final
    : public grpc::ServerBidiReactor<accel::v1::AccelModule,
                                     accel::v1::AccelPacket> {
public:
    UnauthorizedModuleReactor() {
        Finish(
            grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "invalid api key"));
    }

    void OnDone() override { delete this; }
};

class CallbackAccelerometerService final
    : public accel::v1::AccelerometerService::CallbackService {
public:
    explicit CallbackAccelerometerService(AppConfig config)
        : config_(std::move(config)),
          relay_state_(config_.duplicate_precision) {}

    grpc::ServerBidiReactor<accel::v1::AccelPacket, accel::v1::AccelModule>*
    StreamAccelData(grpc::CallbackServerContext* context) override {
        if (!HasValidApiKey(context, config_.api_key)) {
            Logger::Error("callback gRPC node A rejected: invalid api key");
            return new UnauthorizedPacketReactor();
        }
        Logger::Info("callback gRPC node A stream connected");
        return new NodeAReactor(relay_state_);
    }

    grpc::ServerBidiReactor<accel::v1::AccelModule, accel::v1::AccelPacket>*
    ProcessAccelData(grpc::CallbackServerContext* context) override {
        if (!HasValidApiKey(context, config_.api_key)) {
            Logger::Error("callback gRPC node B rejected: invalid api key");
            return new UnauthorizedModuleReactor();
        }
        Logger::Info("callback gRPC node B stream connected");
        return new NodeBReactor(relay_state_);
    }

private:
    AppConfig config_;
    CallbackRelayState relay_state_;
};

template <typename WriteT, typename ReadT>
class QueueingClientReactor : public grpc::ClientBidiReactor<WriteT, ReadT> {
public:
    void Send(WriteT message) {
        std::lock_guard lock(mutex_);
        if (closed_) {
            return;
        }
        write_queue_.push_back(std::move(message));
        TryStartWriteLocked();
    }

    void FinishWrites() {
        std::lock_guard lock(mutex_);
        if (closed_) {
            return;
        }
        writes_done_requested_ = true;
        if (!writing_ && write_queue_.empty()) {
            this->StartWritesDone();
        }
    }

    grpc::Status Wait() {
        std::unique_lock lock(mutex_);
        done_.wait(lock, [this] { return donebool_; });
        return status_;
    }

protected:
    void OnWriteDone(bool ok) override {
        std::lock_guard lock(mutex_);
        if (closed_) {
            return;
        }
        writing_ = false;
        if (!ok) {
            writes_done_requested_ = true;
            return;
        }
        TryStartWriteLocked();
    }

    void OnDone(const grpc::Status& status) override {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
            status_ = status;
            donebool_ = true;
        }
        done_.notify_all();
    }

private:
    void TryStartWriteLocked() {
        if (writing_) {
            return;
        }
        if (closed_) {
            return;
        }
        if (write_queue_.empty()) {
            if (writes_done_requested_) {
                this->StartWritesDone();
            }
            return;
        }
        current_write_ = std::move(write_queue_.front());
        write_queue_.pop_front();
        writing_ = true;
        this->StartWrite(&current_write_);
    }

    std::mutex mutex_;
    std::condition_variable done_;
    std::deque<WriteT> write_queue_;
    WriteT current_write_;
    grpc::Status status_;
    bool writing_{false};
    bool writes_done_requested_{false};
    bool closed_{false};
    bool donebool_{false};
};

class NodeAClientReactor final
    : public QueueingClientReactor<accel::v1::AccelPacket,
                                   accel::v1::AccelModule> {
public:
    NodeAClientReactor(AppConfig config, ModuleLogWriter& log_writer)
        : config_(std::move(config)), log_writer_(log_writer), sensor_(42) {}

    void StartReading() { StartRead(&read_); }

    void StartProducing() {
        writer_ = std::jthread([this](std::stop_token stop) {
            int sent = 0;
            while (!stop.stop_requested() &&
                   (config_.max_samples == 0 || sent < config_.max_samples)) {
                Send(ToProto(sensor_.Next()));
                ++sent;
                std::this_thread::sleep_for(
                    std::chrono::microseconds(1'000'000 / config_.sensor_hz));
            }
            FinishWrites();
        });
    }

private:
    void OnReadDone(bool ok) override {
        if (!ok) {
            return;
        }
        log_writer_.Write(FromProto(read_));
        StartRead(&read_);
    }

    void OnDone(const grpc::Status& status) override {
        writer_.request_stop();
        QueueingClientReactor::OnDone(status);
    }

    AppConfig config_;
    ModuleLogWriter& log_writer_;
    SensorEmulator sensor_;
    accel::v1::AccelModule read_;
    std::jthread writer_;
};

class NodeBClientReactor final
    : public QueueingClientReactor<accel::v1::AccelModule,
                                   accel::v1::AccelPacket> {
public:
    void StartReading() { StartRead(&read_); }

private:
    void OnReadDone(bool ok) override {
        if (!ok) {
            FinishWrites();
            return;
        }
        Send(ToProto(ModuleCalculator::Compute(FromProto(read_))));
        StartRead(&read_);
    }

    accel::v1::AccelPacket read_;
};

}  // namespace

GrpcCallbackServerApp::GrpcCallbackServerApp(AppConfig config)
    : config_(std::move(config)) {}

void GrpcCallbackServerApp::Run() {
    const std::string kAddress = "0.0.0.0:" + std::to_string(config_.grpc_port);
    CallbackAccelerometerService service(config_);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(kAddress, CreateServerCredentials(config_));
    builder.RegisterService(&service);

    auto server = builder.BuildAndStart();
    if (!server) {
        throw std::runtime_error("failed to start callback gRPC server on " +
                                 kAddress);
    }

    Logger::Info("callback gRPC server listening on ", kAddress);
    server->Wait();
}

GrpcCallbackNodeAApp::GrpcCallbackNodeAApp(AppConfig config)
    : config_(std::move(config)) {}

void GrpcCallbackNodeAApp::Run() {
    while (true) {
        ModuleLogWriter log_writer(config_);
        auto stub =
            accel::v1::AccelerometerService::NewStub(CreateChannel(config_));
        grpc::ClientContext context;
        AddApiKey(context, config_.api_key);

        NodeAClientReactor reactor(config_, log_writer);
        stub->async()->StreamAccelData(&context, &reactor);
        reactor.StartReading();
        reactor.StartCall();
        reactor.StartProducing();

        const auto kStatus = reactor.Wait();
        if (kStatus.ok() && config_.max_samples != 0) {
            return;
        }
        Logger::Error("callback gRPC node A stream ended: ",
                      kStatus.error_message());
        std::this_thread::sleep_for(config_.reconnect_delay);
    }
}

GrpcCallbackNodeBApp::GrpcCallbackNodeBApp(AppConfig config)
    : config_(std::move(config)) {}

void GrpcCallbackNodeBApp::Run() {
    while (true) {
        auto stub =
            accel::v1::AccelerometerService::NewStub(CreateChannel(config_));
        grpc::ClientContext context;
        AddApiKey(context, config_.api_key);

        NodeBClientReactor reactor;
        stub->async()->ProcessAccelData(&context, &reactor);
        reactor.StartReading();
        reactor.StartCall();

        const auto kStatus = reactor.Wait();
        Logger::Error("callback gRPC node B stream ended: ",
                      kStatus.error_message());
        std::this_thread::sleep_for(config_.reconnect_delay);
    }
}

}  // namespace accel
