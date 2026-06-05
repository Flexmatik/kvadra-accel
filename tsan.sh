#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${root}/build-tsan"

cmake -S "${root}" -B "${build}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKVADRA_ACCEL_BUILD_TESTS=ON \
  -DKVADRA_ACCEL_BUILD_GRPC=OFF \
  -DCMAKE_CXX_FLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=thread" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread"

cmake --build "${build}" --parallel

TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1" \
ctest --test-dir "${build}" --output-on-failure
