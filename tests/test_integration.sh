#!/usr/bin/env bash
set -euo pipefail

# 集成测试可通过 TLS_TEST_PORT 覆盖端口，便于并行运行。
port="${TLS_TEST_PORT:-18443}"
tmp_dir="$(mktemp -d)"
server_log="$tmp_dir/server.log"
client_log="$tmp_dir/client.log"
server_pid=''

# 测试退出时关闭后台服务端，并清理临时日志目录。
cleanup() {
  if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
    kill -INT "$server_pid"
    wait "$server_pid" || true
  fi
  rm -rf -- "$tmp_dir"
}
trap cleanup EXIT

# 先启动真实服务端，再轮询日志确认监听已经就绪。
./bin/tls_server --port "$port" >"$server_log" 2>&1 &
server_pid=$!
for _ in $(seq 1 50); do
  grep -Fq 'Listening on port' "$server_log" && break
  if ! kill -0 "$server_pid" 2>/dev/null; then
    cat "$server_log" >&2
    exit 1
  fi
  sleep 0.1
done
grep -F 'Listening on port' "$server_log"

# 成功路径验证 TLS 1.2 握手、客户端回显和服务端收包日志。
./bin/tls_client --host localhost --verify-name localhost --port "$port" 'hello TLS 1.2' >"$client_log" 2>&1
grep -F 'Protocol: TLSv1.2' "$client_log"
grep -F 'Echo: hello TLS 1.2' "$client_log"
grep -F 'Received: hello TLS 1.2' "$server_log"

# 以下失败场景都是预期结果，用于验证 CA、主机名和客户端证书约束。
if ./bin/tls_client --host localhost --verify-name localhost --port "$port" --ca certs/client.crt bad-ca; then
  echo 'client unexpectedly trusted the wrong CA' >&2
  exit 1
fi

if ./bin/tls_client --host localhost --verify-name localhost --port "$port" --verify-name invalid.example bad-name; then
  echo 'client unexpectedly accepted the wrong hostname' >&2
  exit 1
fi

# 超过协议上限的消息应在客户端本地被拒绝，不应完成一次成功请求。
oversized="$(head -c 4097 /dev/zero | tr '\0' x)"
if ./bin/tls_client --host localhost --verify-name localhost --port "$port" "$oversized"; then
  echo 'client unexpectedly accepted an oversized message' >&2
  exit 1
fi

# 直接用 openssl s_client 不提供客户端证书，服务端应拒绝 mTLS 握手。
no_cert_log="$tmp_dir/no-cert.log"
timeout 5 openssl s_client -connect "localhost:$port" -tls1_2 \
  -CAfile certs/ca.crt -verify_hostname localhost < /dev/null \
  >"$no_cert_log" 2>&1 || true
LC_ALL=C grep -E \
  'certificate required|handshake failure|alert bad certificate|peer did not return a certificate' \
  "$no_cert_log"
