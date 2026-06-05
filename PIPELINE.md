# KvadraAccel processing pipeline

This document describes how data moves through the system and where each assignment requirement is
implemented.

## Level 1: TCP/JSON Pipeline

```text
Node A sensor emulator
  -> AccelPacket { version, timestamp, x, y, z }
  -> newline-delimited JSON
  -> TCP or TLS TCP connection
  -> Server duplicate filter
  -> TCP or TLS TCP connection
  -> Node B module calculation
  -> AccelModule { version, timestamp, module }
  -> Server reverse relay
  -> Node A module log writer
  -> accel/module.log
```

1. `NodeAApp` reads `sensor_hz` from config and schedules packet generation at that rate.
2. The client sends a `hello` JSON message containing `version`, `role`, and `api_key`.
3. The server validates the API key before accepting data from the connection.
4. The server applies `DuplicateFilter` only to consecutive packets from Node A.
5. Node B receives validated packets, computes `sqrt(x*x + y*y + z*z)`, and sends the module back.
6. Node A appends received modules to `accel/module.log`.

When `KVADRA_ACCEL_BUILD_ASIO_TCP=ON` and Boost.Asio headers are available, Level 1 uses asynchronous
Boost.Asio input/output:

- `async_accept` for server-side connections;
- `async_read_until(..., '\n')` for newline JSON framing;
- `async_write` for outgoing messages;
- `boost::asio::ssl::stream` for TCP TLS/mTLS.

If Boost.Asio is not installed, CMake falls back to the POSIX socket implementation so the project
remains buildable.

## Level 2: gRPC/Protobuf Pipeline

```text
Node A StreamAccelData client stream
  -> gRPC server
  -> duplicate filter
  -> Node B ProcessAccelData server stream
  -> module calculation
  -> gRPC server reverse queue
  -> Node A response stream
```

The `.proto` schema lives in `proto/accelerometer.proto`. The project keeps the required
`StreamAccelData` stream for Node A and adds `ProcessAccelData` for Node B so the server can keep
separate bidirectional streams for the two roles.

Two gRPC runtimes are available:

- synchronous streaming executables: `KvadraAccel_grpc_server`, `KvadraAccel_grpc_node_a`,
  `KvadraAccel_grpc_node_b`;
- callback API executables: `KvadraAccel_grpc_callback_server`, `KvadraAccel_grpc_callback_node_a`,
  `KvadraAccel_grpc_callback_node_b`.

The callback runtime uses `AccelerometerService::CallbackService`,
`grpc::ServerBidiReactor`, and `grpc::ClientBidiReactor`. Server reactors keep one outstanding
read per stream and queue writes so gRPC never receives overlapping writes on the same RPC.

## Level 3: Security Pipeline

The security checks are layered:

1. TLS protects the transport when `tcp_tls` or `grpc_tls` is enabled.
2. mTLS requires a client certificate when `tcp_mutual_tls` or `grpc_mutual_tls` is enabled.
3. The application API key is still checked after the transport is established.

Certificates are generated with:

```bash
./scripts/generate_certs.sh certs
```

The generated CA signs both server and client certificates. Local server certificates include SANs
for `localhost` and `127.0.0.1`.

## Failure Handling

Node A and Node B treat connection loss as expected runtime behavior. They close the current
connection, sleep for `reconnect_delay_ms`, and reconnect. The server keeps listening after a client
disconnects.

Startup/configuration errors are fatal and are reported with a clear error message.
