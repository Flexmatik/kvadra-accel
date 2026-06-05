#include <exception>
#include <iostream>

#include "common/cli.h"
#include "grpc/grpc_level2.h"

int main(int argc, char** argv) {
    try {
        accel::GrpcServerApp app(
            accel::LoadConfigFromArgs(argc, argv, "config/local.json"));
        app.Run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "gRPC server failed: " << error.what() << '\n';
        return 1;
    }
}
