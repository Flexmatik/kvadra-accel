#include "grpc/proto_conversion.h"

namespace accel {

accel::v1::AccelPacket ToProto(const AccelPacket& packet) {
    accel::v1::AccelPacket proto;
    proto.set_version(packet.version);
    proto.set_timestamp(packet.timestamp);
    proto.set_x(static_cast<float>(packet.x));
    proto.set_y(static_cast<float>(packet.y));
    proto.set_z(static_cast<float>(packet.z));
    return proto;
}

AccelPacket FromProto(const accel::v1::AccelPacket& proto) {
    return {
        .version = proto.version(),
        .timestamp = proto.timestamp(),
        .x = proto.x(),
        .y = proto.y(),
        .z = proto.z(),
    };
}

accel::v1::AccelModule ToProto(const AccelModule& module) {
    accel::v1::AccelModule proto;
    proto.set_version(module.version);
    proto.set_timestamp(module.timestamp);
    proto.set_module(static_cast<float>(module.module));
    return proto;
}

AccelModule FromProto(const accel::v1::AccelModule& proto) {
    return {
        .version = proto.version(),
        .timestamp = proto.timestamp(),
        .module = proto.module(),
    };
}

}  // namespace accel