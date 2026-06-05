#pragma once

#include <filesystem>

#include "common/config.h"

namespace accel {

AppConfig LoadConfigFromArgs(int argc, char** argv,
                             const std::filesystem::path& default_path);

}  // namespace accel
