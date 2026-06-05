#pragma once

#include <optional>
#include <string>

namespace accel {

class SocketFd {
public:
    SocketFd() = default;
    explicit SocketFd(int fd);
    ~SocketFd();

    SocketFd(const SocketFd&) = delete;
    SocketFd& operator=(const SocketFd&) = delete;

    SocketFd(SocketFd&& other) noexcept;
    SocketFd& operator=(SocketFd&& other) noexcept;

    [[nodiscard]] int Get() const;
    [[nodiscard]] bool IsValid() const;
    int Release();
    void Reset(int fd = -1);

private:
    int fd_{-1};
};

class TcpConnection {
public:
    explicit TcpConnection(SocketFd fd);

    static TcpConnection Connect(const std::string& host, int port);

    std::string ReadLine();
    std::optional<std::string> ReadLineIfAvailable(int timeout_ms);
    void WriteLine(const std::string& line);

private:
    SocketFd fd_;
};

class TcpListener {
public:
    explicit TcpListener(int port);

    TcpConnection Accept();

private:
    SocketFd fd_;
};

}  // namespace accel
