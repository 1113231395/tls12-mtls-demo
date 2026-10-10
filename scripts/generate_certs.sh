#!/usr/bin/env bash
set -euo pipefail

# 证书输出目录可由调用方指定，默认写入项目的 certs 目录。
output_dir="${1:-certs}"
mkdir -p "$output_dir"

# 在 MSYS 环境下调用 OpenSSL 写文件时，需要转换为原生路径。
native_output_dir="$output_dir"
if command -v cygpath >/dev/null 2>&1; then
  native_output_dir="$(cygpath -m "$output_dir")"
fi

# 生成 demo 根 CA，后续用它签发服务端和客户端证书。
openssl genrsa -out "$output_dir/ca.key" 2048
chmod 600 "$output_dir/ca.key"
MSYS_NO_PATHCONV=1 openssl req -x509 -new -sha256 \
  -key "$native_output_dir/ca.key" \
  -out "$native_output_dir/ca.crt" \
  -days 3650 \
  -subj '/CN=TLS Demo Root CA'

# 服务端证书带 serverAuth 和目标设备连接的 IP SAN，供客户端做主机名校验。
cat > "$output_dir/server.ext" <<'EOF'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost,IP:127.0.0.1,IP:192.168.168.142
EOF

# 客户端证书只声明 clientAuth，体现双向认证中的客户端身份。
cat > "$output_dir/client.ext" <<'EOF'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=clientAuth
EOF

# 生成服务端私钥、CSR，并用 demo CA 签发带 SAN 的服务端证书。
openssl genrsa -out "$output_dir/server.key" 2048
chmod 600 "$output_dir/server.key"
MSYS_NO_PATHCONV=1 openssl req -new \
  -key "$native_output_dir/server.key" \
  -out "$native_output_dir/server.csr" \
  -subj '/CN=localhost'
openssl x509 -req -sha256 \
  -in "$output_dir/server.csr" \
  -CA "$output_dir/ca.crt" \
  -CAkey "$output_dir/ca.key" \
  -CAcreateserial \
  -CAserial "$output_dir/ca.srl" \
  -out "$output_dir/server.crt" \
  -days 365 \
  -extfile "$output_dir/server.ext"

# 生成客户端私钥、CSR，并用同一个 demo CA 签发客户端证书。
openssl genrsa -out "$output_dir/client.key" 2048
chmod 600 "$output_dir/client.key"
MSYS_NO_PATHCONV=1 openssl req -new \
  -key "$native_output_dir/client.key" \
  -out "$native_output_dir/client.csr" \
  -subj '/CN=demo-client'
openssl x509 -req -sha256 \
  -in "$output_dir/client.csr" \
  -CA "$output_dir/ca.crt" \
  -CAkey "$output_dir/ca.key" \
  -CAserial "$output_dir/ca.srl" \
  -out "$output_dir/client.crt" \
  -days 365 \
  -extfile "$output_dir/client.ext"

# 签发中间文件无需保留，证书和私钥留在输出目录供 demo 使用。
rm -f \
  "$output_dir/server.csr" \
  "$output_dir/client.csr" \
  "$output_dir/ca.srl" \
  "$output_dir/server.ext" \
  "$output_dir/client.ext"
