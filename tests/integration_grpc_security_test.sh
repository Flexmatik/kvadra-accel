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

base_port=$((30000 + (BASHPID % 20000)))
node_a_port="${base_port}"
node_b_port="$((base_port + 1))"
grpc_port="$((base_port + 2))"
cert_dir="${work_dir}/certs"
module_log="${work_dir}/accel/module.log"
config="${work_dir}/secure-grpc.json"
bad_config="${work_dir}/bad-api-key.json"

bash "${source_dir}/scripts/generate_certs.sh" "${cert_dir}" >/dev/null

write_config() {
  local path="$1"
  local api_key="$2"
  cat >"${path}" <<JSON
{
  "server_host": "127.0.0.1",
  "node_a_port": ${node_a_port},
  "node_b_port": ${node_b_port},
  "grpc_port": ${grpc_port},
  "api_key": "${api_key}",
  "sensor_hz": 50,
  "reconnect_delay_ms": 100,
  "duplicate_precision": 3,
  "module_log_path": "${module_log}",
  "max_samples": 20,
  "grpc_tls": true,
  "grpc_mutual_tls": true,
  "tcp_tls": false,
  "tcp_mutual_tls": false,
  "tls_target_name": "localhost",
  "ca_cert_path": "${cert_dir}/ca.crt",
  "server_cert_path": "${cert_dir}/server.crt",
  "server_key_path": "${cert_dir}/server.key",
  "client_cert_path": "${cert_dir}/client.crt",
  "client_key_path": "${cert_dir}/client.key"
}
JSON
}

write_config "${config}" "integration-test-api-key"
write_config "${bad_config}" "wrong-api-key"

"${server_bin}" "${config}" >"${work_dir}/server.log" 2>&1 &
server_pid="$!"
sleep 1

"${node_b_bin}" "${config}" >"${work_dir}/node_b.log" 2>&1 &
node_b_pid="$!"
sleep 0.5

if timeout 5 "${node_a_bin}" "${bad_config}" >"${work_dir}/bad_node_a.log" 2>&1; then
  echo "Node A succeeded with an invalid API key" >&2
  exit 1
fi

timeout 15 "${node_a_bin}" "${config}" >"${work_dir}/node_a.log" 2>&1

if [[ ! -s "${module_log}" ]]; then
  echo "secure gRPC module log was not created or is empty" >&2
  echo "--- server.log ---" >&2
  sed -n '1,200p' "${work_dir}/server.log" >&2 || true
  echo "--- node_b.log ---" >&2
  sed -n '1,200p' "${work_dir}/node_b.log" >&2 || true
  echo "--- node_a.log ---" >&2
  sed -n '1,200p' "${work_dir}/node_a.log" >&2 || true
  exit 1
fi

if ! grep -q "invalid api key" "${work_dir}/server.log"; then
  echo "server did not log invalid API-key rejection" >&2
  sed -n '1,200p' "${work_dir}/server.log" >&2 || true
  exit 1
fi
