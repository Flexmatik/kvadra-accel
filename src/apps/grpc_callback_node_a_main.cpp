#include <exception>
#include <iostream>

#include "common/cli.h"
#include "grpc/grpc_level2.h"

int main(int argc, char** argv) {
    try {
        accel::GrpcCallbackNodeAApp app(
            accel::LoadConfigFromArgs(argc, argv, "config/local.json"));
        app.Run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "callback gRPC node A failed: " << error.what() << '\n';
        return 1;
    }
}
