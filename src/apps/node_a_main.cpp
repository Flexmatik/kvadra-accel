#include <exception>
#include <iostream>

#include "common/cli.h"
#include "tcp/level1.h"

int main(int argc, char** argv) {
    try {
        accel::NodeAApp app(
            accel::LoadConfigFromArgs(argc, argv, "config/local.json"));
        app.Run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "node A failed: " << error.what() << '\n';
        return 1;
    }
}
