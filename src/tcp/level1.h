#pragma once

#include "common/blocking_queue.h"
#include "common/config.h"
#include "common/domain.h"

namespace accel {

class ModuleLogWriter {
public:
    explicit ModuleLogWriter(const AppConfig& config);

    void Write(const AccelModule& module);

private:
    std::filesystem::path path_;
};

class ServerApp {
public:
    explicit ServerApp(AppConfig config);

    void Run();

private:
    void AcceptNodeA();
    void AcceptNodeB();
    void HandleNodeA(class TcpConnection connection);
    void HandleNodeB(class TcpConnection connection);

    AppConfig config_;
    DuplicateFilter duplicate_filter_;
    BlockingQueue<AccelPacket> packets_to_b_;
    BlockingQueue<AccelModule> modules_to_a_;
};

class NodeAApp {
public:
    explicit NodeAApp(AppConfig config);

    void Run();

private:
    AppConfig config_;
};

class NodeBApp {
public:
    explicit NodeBApp(AppConfig config);

    void Run();

private:
    AppConfig config_;
};

}  // namespace accel
