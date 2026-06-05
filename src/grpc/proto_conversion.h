#pragma once
#include "accelerometer.pb.h"
#include "common/domain.h"

namespace accel {

accel::v1::AccelPacket ToProto(const AccelPacket& packet);
AccelPacket FromProto(const accel::v1::AccelPacket& proto);

accel::v1::AccelModule ToProto(const AccelModule& module);
AccelModule FromProto(const accel::v1::AccelModule& proto);

}  // namespace accel
