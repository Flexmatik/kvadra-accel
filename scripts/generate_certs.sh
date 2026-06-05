#!/usr/bin/env bash
set -euo pipefail

out_dir="${1:-certs}"
mkdir -p "${out_dir}"

cat >"${out_dir}/server.ext" <<'EOF'
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = DNS:localhost,IP:127.0.0.1
EOF

cat >"${out_dir}/client.ext" <<'EOF'
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = clientAuth
subjectAltName = DNS:KvadraAccel-client
EOF

openssl genrsa -out "${out_dir}/ca.key" 4096
openssl req -x509 -new -nodes \
  -key "${out_dir}/ca.key" \
  -sha256 \
  -days 3650 \
  -subj "/CN=KvadraAccel-local-ca" \
  -out "${out_dir}/ca.crt"

openssl genrsa -out "${out_dir}/server.key" 2048
openssl req -new \
  -key "${out_dir}/server.key" \
  -subj "/CN=localhost" \
  -out "${out_dir}/server.csr"
openssl x509 -req \
  -in "${out_dir}/server.csr" \
  -CA "${out_dir}/ca.crt" \
  -CAkey "${out_dir}/ca.key" \
  -CAcreateserial \
  -out "${out_dir}/server.crt" \
  -days 825 \
  -sha256 \
  -extfile "${out_dir}/server.ext"

openssl genrsa -out "${out_dir}/client.key" 2048
openssl req -new \
  -key "${out_dir}/client.key" \
  -subj "/CN=KvadraAccel-client" \
  -out "${out_dir}/client.csr"
openssl x509 -req \
  -in "${out_dir}/client.csr" \
  -CA "${out_dir}/ca.crt" \
  -CAkey "${out_dir}/ca.key" \
  -CAcreateserial \
  -out "${out_dir}/client.crt" \
  -days 825 \
  -sha256 \
  -extfile "${out_dir}/client.ext"

rm -f "${out_dir}/server.csr" "${out_dir}/client.csr" "${out_dir}/server.ext" "${out_dir}/client.ext"

chmod 600 "${out_dir}"/*.key

echo "Generated certificates in ${out_dir}"
