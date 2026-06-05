#include "common/socket.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace accel {
namespace {

std::runtime_error SocketError(const std::string& action) {
    return std::runtime_error(action + ": " + std::strerror(errno));
}

void WriteAll(int fd, const char* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t kResult = ::send(fd, data + sent, size - sent, 0);
        if (kResult <= 0) {
            throw SocketError("send");
        }
        sent += static_cast<std::size_t>(kResult);
    }
}

sockaddr_in MakeAddress(const std::string& host, int port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid IPv4 address: " + host);
    }
    return address;
}

}  // namespace

SocketFd::SocketFd(int fd) : fd_(fd) {}

SocketFd::~SocketFd() { Reset(); }

SocketFd::SocketFd(SocketFd&& other) noexcept : fd_(other.Release()) {}

SocketFd& SocketFd::operator=(SocketFd&& other) noexcept {
    if (this != &other) {
        Reset(other.Release());
    }
    return *this;
}

int SocketFd::Get() const { return fd_; }

bool SocketFd::IsValid() const { return fd_ >= 0; }

int SocketFd::Release() {
    const int kFd = fd_;
    fd_ = -1;
    return kFd;
}

void SocketFd::Reset(int fd) {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = fd;
}

TcpConnection::TcpConnection(SocketFd fd) : fd_(std::move(fd)) {}

TcpConnection TcpConnection::Connect(const std::string& host, int port) {
    SocketFd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd.IsValid()) {
        throw SocketError("socket");
    }

    const auto kAddress = MakeAddress(host, port);
    if (::connect(fd.Get(), reinterpret_cast<const sockaddr*>(&kAddress),
                  sizeof(kAddress)) != 0) {
        throw SocketError("connect");
    }
    return TcpConnection(std::move(fd));
}

std::string TcpConnection::ReadLine() {
    std::string line;
    char ch = '\0';
    while (true) {
        const ssize_t kResult = ::recv(fd_.Get(), &ch, 1, 0);
        if (kResult == 0) {
            throw std::runtime_error("peer closed connection");
        }
        if (kResult < 0) {
            throw SocketError("recv");
        }
        if (ch == '\n') {
            return line;
        }
        line.push_back(ch);
    }
}

std::optional<std::string> TcpConnection::ReadLineIfAvailable(int timeout_ms) {
    pollfd descriptor{.fd = fd_.Get(), .events = POLLIN, .revents = 0};
    const int kResult = ::poll(&descriptor, 1, timeout_ms);
    if (kResult == 0) {
        return std::nullopt;
    }
    if (kResult < 0) {
        throw SocketError("poll");
    }
    return ReadLine();
}

void TcpConnection::WriteLine(const std::string& line) {
    WriteAll(fd_.Get(), line.data(), line.size());
    WriteAll(fd_.Get(), "\n", 1);
}

TcpListener::TcpListener(int port) {
    SocketFd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd.IsValid()) {
        throw SocketError("socket");
    }

    int reuse = 1;
    if (::setsockopt(fd.Get(), SOL_SOCKET, SO_REUSEADDR, &reuse,
                     sizeof(reuse)) != 0) {
        throw SocketError("setsockopt");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(fd.Get(), reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) != 0) {
        throw SocketError("bind");
    }
    if (::listen(fd.Get(), 8) != 0) {
        throw SocketError("listen");
    }

    fd_ = std::move(fd);
}

TcpConnection TcpListener::Accept() {
    const int kClient = ::accept(fd_.Get(), nullptr, nullptr);
    if (kClient < 0) {
        throw SocketError("accept");
    }
    return TcpConnection(SocketFd(kClient));
}

}  // namespace accel
