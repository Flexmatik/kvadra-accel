#pragma once

#include "common/config.h"
#include "common/domain.h"

namespace accel {

class GrpcServerApp {
public:
    explicit GrpcServerApp(AppConfig config);

    void Run();

private:
    AppConfig config_;
};

class GrpcNodeAApp {
public:
    explicit GrpcNodeAApp(AppConfig config);

    void Run();

private:
    AppConfig config_;
};

class GrpcNodeBApp {
public:
    explicit GrpcNodeBApp(AppConfig config);

    void Run();

private:
    AppConfig config_;
};

class GrpcCallbackServerApp {
public:
    explicit GrpcCallbackServerApp(AppConfig config);

    void Run();

private:
    AppConfig config_;
};

class GrpcCallbackNodeAApp {
public:
    explicit GrpcCallbackNodeAApp(AppConfig config);

    void Run();

private:
    AppConfig config_;
};

class GrpcCallbackNodeBApp {
public:
    explicit GrpcCallbackNodeBApp(AppConfig config);

    void Run();

private:
    AppConfig config_;
};

}  // namespace accel
