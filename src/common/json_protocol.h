#pragma once

#include <string>

#include "common/domain.h"

namespace accel {

enum class ClientRole {
    NodeA,
    NodeB,
};

struct HelloMessage {
    int version{kProtocolVersion};
    ClientRole role{ClientRole::NodeA};
    std::string api_key;
};

std::string RoleToString(ClientRole role);
ClientRole ParseRole(const std::string& value);

std::string SerializeHello(const HelloMessage& hello);
std::string SerializePacket(const AccelPacket& packet);
std::string SerializeModule(const AccelModule& module);

HelloMessage ParseHello(const std::string& line);
AccelPacket ParsePacket(const std::string& line);
AccelModule ParseModule(const std::string& line);

bool IsAuthorized(const HelloMessage& hello,
                  const std::string& expected_api_key,
                  ClientRole expected_role);

}  // namespace accel
