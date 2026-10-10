#!/usr/bin/env bash
set -euo pipefail

# 在临时目录中生成证书，避免污染项目 certs 目录。
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf -- "$tmp_dir"' EXIT

# 调用正式证书脚本，测试内容覆盖脚本真实输出。
"$root_dir/scripts/generate_certs.sh" "$tmp_dir"

# 服务端和客户端证书必须分别满足对应的 TLS 用途校验。
openssl verify -CAfile "$tmp_dir/ca.crt" -purpose sslserver "$tmp_dir/server.crt"
openssl verify -CAfile "$tmp_dir/ca.crt" -purpose sslclient "$tmp_dir/client.crt"

# 服务端证书必须包含 localhost 的 DNS/IP SAN，供客户端主机名校验。
openssl x509 -in "$tmp_dir/server.crt" -noout -ext subjectAltName |
  grep -F 'DNS:localhost, IP Address:127.0.0.1'

# 扩展密钥用途区分服务端和客户端证书角色。
openssl x509 -in "$tmp_dir/server.crt" -noout -ext extendedKeyUsage |
  grep -F 'TLS Web Server Authentication'
openssl x509 -in "$tmp_dir/client.crt" -noout -ext extendedKeyUsage |
  grep -F 'TLS Web Client Authentication'

# 私钥文件权限必须限制为所有者可读写。
test "$(stat -c '%a' "$tmp_dir/server.key")" = 600
test "$(stat -c '%a' "$tmp_dir/client.key")" = 600
