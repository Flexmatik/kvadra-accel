#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
  echo "usage: $0 <server-bin> <node-a-bin> <node-b-bin> <source-dir>" >&2
  exit 2
fi

server_bin="$1"
node_a_bin="$2"
node_b_bin="$3"
source_dir="$4"

work_dir="$(mktemp -d)"
server_pid=""
node_b_pid=""

cleanup() {
  if [[ -n "${node_b_pid}" ]]; then
    kill "${node_b_pid}" 2>/dev/null || true
  fi
  if [[ -n "${server_pid}" ]]; then
    kill "${server_pid}" 2>/dev/null || true
  fi
  wait 2>/dev/null || true
  rm -rf "${work_dir}"
}
trap cleanup EXIT

base_port=$((20000 + (BASHPID % 20000)))
node_a_port="${base_port}"
node_b_port="$((base_port + 1))"
grpc_port="$((base_port + 2))"
module_log="${work_dir}/accel/module.log"
config="${work_dir}/level1.json"

cat >"${config}" <<JSON
{
  "server_host": "127.0.0.1",
  "node_a_port": ${node_a_port},
  "node_b_port": ${node_b_port},
  "grpc_port": ${grpc_port},
  "api_key": "e2e-test-api-key",
  "sensor_hz": 50,
  "reconnect_delay_ms": 100,
  "duplicate_precision": 3,
  "module_log_path": "${module_log}",
  "max_samples": 60,
  "grpc_tls": false,
  "grpc_mutual_tls": false,
  "tcp_tls": false,
  "tcp_mutual_tls": false,
  "tls_target_name": "localhost",
  "ca_cert_path": "${source_dir}/certs/ca.crt",
  "server_cert_path": "${source_dir}/certs/server.crt",
  "server_key_path": "${source_dir}/certs/server.key",
  "client_cert_path": "${source_dir}/certs/client.crt",
  "client_key_path": "${source_dir}/certs/client.key"
}
JSON

"${server_bin}" "${config}" >"${work_dir}/server.log" 2>&1 &
server_pid="$!"
sleep 0.5
if ! kill -0 "${server_pid}" 2>/dev/null; then
  echo "server exited before clients connected" >&2
  sed -n '1,160p' "${work_dir}/server.log" >&2 || true
  exit 1
fi

"${node_b_bin}" "${config}" >"${work_dir}/node_b.log" 2>&1 &
node_b_pid="$!"
sleep 0.3

timeout 15 "${node_a_bin}" "${config}" >"${work_dir}/node_a.log" 2>&1

if [[ ! -s "${module_log}" ]]; then
  echo "module log was not created or is empty" >&2
  echo "--- server.log ---" >&2
  sed -n '1,160p' "${work_dir}/server.log" >&2 || true
  echo "--- node_b.log ---" >&2
  sed -n '1,160p' "${work_dir}/node_b.log" >&2 || true
  echo "--- node_a.log ---" >&2
  sed -n '1,160p' "${work_dir}/node_a.log" >&2 || true
  exit 1
fi

awk 'NF != 2 || $1 !~ /^[0-9]+$/ || $2 !~ /^-?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/ { exit 1 }' "${module_log}"
