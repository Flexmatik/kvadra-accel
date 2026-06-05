#include "tcp/level1.h"

#ifdef KVADRA_ACCEL_HAS_ASIO_TCP

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <deque>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>
#include <variant>

#include "common/json_protocol.h"
#include "common/logger.h"

namespace accel {
namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = asio::ip::tcp;
using AnyExecutor = asio::any_io_executor;

namespace {

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open TLS file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

ssl::context MakeServerSslContext(const AppConfig& config) {
    ssl::context context(ssl::context::tls_server);
    context.set_options(ssl::context::default_workarounds |
                        ssl::context::no_sslv2 | ssl::context::no_sslv3);
    context.use_certificate_chain_file(config.server_cert_path.string());
    context.use_private_key_file(config.server_key_path.string(),
                                 ssl::context::pem);
    context.load_verify_file(config.ca_cert_path.string());
    context.set_verify_mode(config.tcp_mutual_tls
                                ? ssl::verify_peer |
                                      ssl::verify_fail_if_no_peer_cert
                                : ssl::verify_none);
    return context;
}

ssl::context MakeClientSslContext(const AppConfig& config) {
    ssl::context context(ssl::context::tls_client);
    context.set_options(ssl::context::default_workarounds |
                        ssl::context::no_sslv2 | ssl::context::no_sslv3);
    context.load_verify_file(config.ca_cert_path.string());
    if (config.tcp_mutual_tls) {
        context.use_certificate_chain_file(config.client_cert_path.string());
        context.use_private_key_file(config.client_key_path.string(),
                                     ssl::context::pem);
    }
    context.set_verify_mode(ssl::verify_peer);
    return context;
}

class AsyncLineConnection {
public:
    AsyncLineConnection(tcp::socket socket, const AppConfig& config,
                        bool server_side)
        : stream_(std::move(socket)),
          config_(config),
          server_side_(server_side) {}

    AsyncLineConnection(tcp::socket socket, ssl::context& context,
                        const AppConfig& config, bool server_side)
        : stream_(ssl::stream<tcp::socket>(std::move(socket), context)),
          config_(config),
          server_side_(server_side) {}

    void Start() {
        if (auto* tls = std::get_if<ssl::stream<tcp::socket>>(&stream_)) {
            if (!server_side_) {
                SSL_set_tlsext_host_name(tls->native_handle(),
                                         config_.tls_target_name.c_str());
                tls->set_verify_callback(
                    ssl::host_name_verification(config_.tls_target_name));
            }

            tls->async_handshake(server_side_ ? ssl::stream_base::server
                                              : ssl::stream_base::client,
                                 [self = shared_from_this()](
                                     const boost::system::error_code& error) {
                                     if (error) {
                                         self->Fail(error, "TLS handshake");
                                         return;
                                     }
                                     self->DoRead();
                                     if (self->on_ready) {
                                         self->on_ready();
                                     }
                                 });
            return;
        }

        DoRead();
        if (on_ready) {
            on_ready();
        }
    }

    void WriteLine(std::string line) {
        line.push_back('\n');
        asio::post(GetExecutor(), [self = shared_from_this(),
                                   line = std::move(line)]() mutable {
            const bool kIdle = self->write_queue_.empty();
            self->write_queue_.push_back(std::move(line));
            if (kIdle) {
                self->DoWrite();
            }
        });
    }

    std::function<void(const std::string&)> on_line;
    std::function<void()> on_close;
    std::function<void()> on_ready;

private:
    using Stream = std::variant<tcp::socket, ssl::stream<tcp::socket>>;

    std::shared_ptr<AsyncLineConnection> shared_from_this() {  // NOLINT
        return self_.lock();
    }

public:
    void BindSelf(const std::shared_ptr<AsyncLineConnection>& self) {
        self_ = self;
    }

private:
    AnyExecutor GetExecutor() {
        return std::visit([](auto& stream) { return stream.get_executor(); },
                          stream_);
    }

    template <typename Handler>
    void AsyncReadUntil(Handler&& handler) {
        std::visit(
            [&](auto& stream) {
                asio::async_read_until(stream, read_buffer_, '\n',
                                       std::forward<Handler>(handler));
            },
            stream_);
    }

    template <typename Handler>
    void AsyncWrite(const std::string& data, Handler&& handler) {
        std::visit(
            [&](auto& stream) {
                asio::async_write(stream, asio::buffer(data),
                                  std::forward<Handler>(handler));
            },
            stream_);
    }

    void DoRead() {
        AsyncReadUntil(
            [self = shared_from_this()](const boost::system::error_code& error,
                                        std::size_t) {
                if (error) {
                    self->Fail(error, "read");
                    return;
                }

                std::istream input(&self->read_buffer_);
                std::string line;
                std::getline(input, line);
                if (self->on_line) {
                    self->on_line(line);
                }
                self->DoRead();
            });
    }

    void DoWrite() {
        AsyncWrite(write_queue_.front(),
                   [self = shared_from_this()](
                       const boost::system::error_code& error, std::size_t) {
                       if (error) {
                           self->Fail(error, "write");
                           return;
                       }

                       self->write_queue_.pop_front();
                       if (!self->write_queue_.empty()) {
                           self->DoWrite();
                       }
                   });
    }

    void Fail(const boost::system::error_code& error,
              const char* action) const {
        Logger::Error("async TCP ", action, " failed: ", error.message());
        if (on_close) {
            on_close();
        }
    }

    Stream stream_;
    AppConfig config_;
    bool server_side_{false};
    asio::streambuf read_buffer_;
    std::deque<std::string> write_queue_;
    std::weak_ptr<AsyncLineConnection> self_;
};

std::shared_ptr<AsyncLineConnection> MakeConnection(tcp::socket socket,
                                                    ssl::context* ssl_context,
                                                    const AppConfig& config,
                                                    bool server_side) {
    std::shared_ptr<AsyncLineConnection> connection;
    if (ssl_context != nullptr) {
        connection = std::make_shared<AsyncLineConnection>(
            std::move(socket), *ssl_context, config, server_side);
    } else {
        connection = std::make_shared<AsyncLineConnection>(std::move(socket),
                                                           config, server_side);
    }
    connection->BindSelf(connection);
    return connection;
}

void SleepBeforeReconnect(const AppConfig& config) {
    Logger::Info("reconnecting in ", config.reconnect_delay.count(), " ms");
    std::this_thread::sleep_for(config.reconnect_delay);
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

void ServerApp::Run() {  // NOLINT
    asio::io_context io;
    std::optional<ssl::context> ssl_context;
    if (config_.tcp_tls) {
        ssl_context.emplace(MakeServerSslContext(config_));
    }

    tcp::acceptor accept_a(
        io, tcp::endpoint(tcp::v4(),
                          static_cast<unsigned short>(config_.node_a_port)));
    tcp::acceptor accept_b(
        io, tcp::endpoint(tcp::v4(),
                          static_cast<unsigned short>(config_.node_b_port)));
    std::shared_ptr<AsyncLineConnection> node_a;
    std::shared_ptr<AsyncLineConnection> node_b;

    std::function<void()> accept_node_a;
    std::function<void()> accept_node_b;

    accept_node_a = [&] {
        accept_a.async_accept([&](const boost::system::error_code& error,
                                  tcp::socket socket) {
            if (!error) {
                Logger::Info("async TCP node A connected");
                node_a = MakeConnection(std::move(socket),
                                        ssl_context ? &*ssl_context : nullptr,
                                        config_, true);
                auto authorized = std::make_shared<bool>(false);
                node_a->on_line = [this, authorized,
                                   &node_b](const std::string& line) {
                    if (!*authorized) {
                        *authorized =
                            IsAuthorized(ParseHello(line), config_.api_key,
                                         ClientRole::NodeA);
                        if (!*authorized) {
                            Logger::Error(
                                "node A rejected: invalid version, role, or "
                                "api key");
                        }
                        return;
                    }

                    const auto kPacket = ParsePacket(line);
                    if (duplicate_filter_.Accept(kPacket)) {
                        if (node_b) {
                            node_b->WriteLine(SerializePacket(kPacket));
                        }
                    } else {
                        Logger::Info("duplicate packet dropped at timestamp ",
                                     kPacket.timestamp);
                    }
                };
                node_a->on_close = [&] {
                    node_a.reset();
                    accept_node_a();
                };
                node_a->Start();
            } else {
                Logger::Error("node A accept failed: ", error.message());
                accept_node_a();
            }
        });
    };

    accept_node_b = [&] {
        accept_b.async_accept([&](const boost::system::error_code& error,
                                  tcp::socket socket) {
            if (!error) {
                Logger::Info("async TCP node B connected");
                node_b = MakeConnection(std::move(socket),
                                        ssl_context ? &*ssl_context : nullptr,
                                        config_, true);
                auto authorized = std::make_shared<bool>(false);
                node_b->on_line = [this, authorized,
                                   &node_a](const std::string& line) {
                    if (!*authorized) {
                        *authorized =
                            IsAuthorized(ParseHello(line), config_.api_key,
                                         ClientRole::NodeB);
                        if (!*authorized) {
                            Logger::Error(
                                "node B rejected: invalid version, role, or "
                                "api key");
                        }
                        return;
                    }

                    if (node_a) {
                        node_a->WriteLine(SerializeModule(ParseModule(line)));
                    }
                };
                node_b->on_close = [&] {
                    node_b.reset();
                    accept_node_b();
                };
                node_b->Start();
            } else {
                Logger::Error("node B accept failed: ", error.message());
                accept_node_b();
            }
        });
    };

    Logger::Info("async TCP server starting on ports A=", config_.node_a_port,
                 " B=", config_.node_b_port,
                 config_.tcp_tls ? " with TLS" : "");
    accept_node_a();
    accept_node_b();
    io.run();
}

NodeAApp::NodeAApp(AppConfig config) : config_(std::move(config)) {}

void NodeAApp::Run() {
    while (true) {
        try {
            asio::io_context io;
            std::optional<ssl::context> ssl_context;
            if (config_.tcp_tls) {
                ssl_context.emplace(MakeClientSslContext(config_));
            }

            ModuleLogWriter log_writer(config_);
            SensorEmulator sensor;
            tcp::resolver resolver(io);
            auto endpoints = resolver.resolve(
                config_.server_host, std::to_string(config_.node_a_port));
            tcp::socket socket(io);
            asio::connect(socket, endpoints);
            auto connection = MakeConnection(
                std::move(socket), ssl_context ? &*ssl_context : nullptr,
                config_, false);

            asio::steady_timer timer(io);
            int sent = 0;
            std::function<void()> send_next = [&] {
                if (config_.max_samples != 0 && sent >= config_.max_samples) {
                    io.stop();
                    return;
                }
                connection->WriteLine(SerializePacket(sensor.Next()));
                ++sent;
                timer.expires_after(
                    std::chrono::microseconds(1'000'000 / config_.sensor_hz));
                timer.async_wait([&](const boost::system::error_code& error) {
                    if (!error) {
                        send_next();
                    }
                });
            };

            connection->on_line = [&](const std::string& line) {
                log_writer.Write(ParseModule(line));
            };
            connection->on_close = [&] { io.stop(); };
            connection->on_ready = [&] {
                connection->WriteLine(SerializeHello(
                    {.role = ClientRole::NodeA, .api_key = config_.api_key}));
                send_next();
            };
            connection->Start();
            io.run();
            if (config_.max_samples != 0 && sent >= config_.max_samples) {
                return;
            }
        } catch (const std::exception& error) {
            Logger::Error("async TCP node A error: ", error.what());
        }
        SleepBeforeReconnect(config_);
    }
}

NodeBApp::NodeBApp(AppConfig config) : config_(std::move(config)) {}

void NodeBApp::Run() {
    while (true) {
        try {
            asio::io_context io;
            std::optional<ssl::context> ssl_context;
            if (config_.tcp_tls) {
                ssl_context.emplace(MakeClientSslContext(config_));
            }

            tcp::resolver resolver(io);
            auto endpoints = resolver.resolve(
                config_.server_host, std::to_string(config_.node_b_port));
            tcp::socket socket(io);
            asio::connect(socket, endpoints);
            auto connection = MakeConnection(
                std::move(socket), ssl_context ? &*ssl_context : nullptr,
                config_, false);

            connection->on_line = [&](const std::string& line) {
                connection->WriteLine(SerializeModule(
                    ModuleCalculator::Compute(ParsePacket(line))));
            };
            connection->on_close = [&] { io.stop(); };
            connection->on_ready = [&] {
                connection->WriteLine(SerializeHello(
                    {.role = ClientRole::NodeB, .api_key = config_.api_key}));
            };
            connection->Start();
            io.run();
        } catch (const std::exception& error) {
            Logger::Error("async TCP node B error: ", error.what());
        }
        SleepBeforeReconnect(config_);
    }
}

}  // namespace accel

#endif
