#include "common/json_protocol.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace accel {

std::string RoleToString(ClientRole role) {
    switch (role) {
        case ClientRole::NodeA:
            return "node_a";
        case ClientRole::NodeB:
            return "node_b";
    }
    throw std::runtime_error("unknown client role");
}

ClientRole ParseRole(const std::string& value) {
    if (value == "node_a") {
        return ClientRole::NodeA;
    }
    if (value == "node_b") {
        return ClientRole::NodeB;
    }
    throw std::runtime_error("unknown client role: " + value);
}

std::string SerializeHello(const HelloMessage& hello) {
    return nlohmann::json{
        {"type", "hello"},
        {"version", hello.version},
        {"role", RoleToString(hello.role)},
        {"api_key", hello.api_key},
    }
        .dump();
}

std::string SerializePacket(const AccelPacket& packet) {
    return nlohmann::json{
        {"type", "accel"},
        {"version", packet.version},
        {"timestamp", packet.timestamp},
        {"x", packet.x},
        {"y", packet.y},
        {"z", packet.z},
    }
        .dump();
}

std::string SerializeModule(const AccelModule& module) {
    return nlohmann::json{
        {"type", "module"},
        {"version", module.version},
        {"timestamp", module.timestamp},
        {"module", module.module},
    }
        .dump();
}

HelloMessage ParseHello(const std::string& line) {
    const auto kJson = nlohmann::json::parse(line);
    if (kJson.at("type").get<std::string>() != "hello") {
        throw std::runtime_error("expected hello message");
    }
    return {
        .version = kJson.at("version").get<int>(),
        .role = ParseRole(kJson.at("role").get<std::string>()),
        .api_key = kJson.at("api_key").get<std::string>(),
    };
}

AccelPacket ParsePacket(const std::string& line) {
    const auto kJson = nlohmann::json::parse(line);
    if (kJson.at("type").get<std::string>() != "accel") {
        throw std::runtime_error("expected accel message");
    }
    return {
        .version = kJson.at("version").get<int>(),
        .timestamp = kJson.at("timestamp").get<std::int64_t>(),
        .x = kJson.at("x").get<double>(),
        .y = kJson.at("y").get<double>(),
        .z = kJson.at("z").get<double>(),
    };
}

AccelModule ParseModule(const std::string& line) {
    const auto kJson = nlohmann::json::parse(line);
    if (kJson.at("type").get<std::string>() != "module") {
        throw std::runtime_error("expected module message");
    }
    return {
        .version = kJson.at("version").get<int>(),
        .timestamp = kJson.at("timestamp").get<std::int64_t>(),
        .module = kJson.at("module").get<double>(),
    };
}

bool IsAuthorized(const HelloMessage& hello,
                  const std::string& expected_api_key,
                  ClientRole expected_role) {
    return hello.version == kProtocolVersion && hello.role == expected_role &&
           hello.api_key == expected_api_key;
}

}  // namespace accel
