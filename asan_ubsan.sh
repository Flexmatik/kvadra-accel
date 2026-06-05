#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${root}/build-asan-ubsan"

cmake -S "${root}" -B "${build}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKVADRA_ACCEL_BUILD_TESTS=ON \
  -DKVADRA_ACCEL_BUILD_GRPC=OFF \
  -DCMAKE_CXX_FLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"

cmake --build "${build}" --parallel

ASAN_OPTIONS="detect_leaks=1:strict_init_order=1:abort_on_error=1" \
UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
ctest --test-dir "${build}" --output-on-failure