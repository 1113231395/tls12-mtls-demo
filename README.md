# TLS 1.2 mTLS TCP Demo

这是一个面向 Linux 的纯 C 示例工程。服务端和客户端通过 OpenSSL 建立双向 TLS 1.2（mTLS）连接，再使用“4 字节网络字节序长度 + 原始载荷”的消息格式完成一次字符串请求和回显。

该工程用于演示如何在现有 TCP 通信外增加 TLS 传输保护。它没有实现 JT/T 808 消息编解码；实际接入时，可在 TLS 连接建立后继续复用现有的 808 数据帧处理逻辑。

## 功能概览

- 服务端验证客户端证书，客户端验证服务端证书。
- 客户端校验服务端证书中的主机名或 IP。
- 双方固定使用 TLS 1.2。
- 每次连接发送一条 1 至 4096 字节的消息，服务端收到后原样回显。
- 支持本机编译运行，也支持交叉编译 ARM 客户端。

## 环境要求

本机运行环境为 Linux，需要 C 编译器、GNU Make、pkg-config、OpenSSL 开发库和 OpenSSL 命令行工具。

本机 OpenSSL 支持 OpenSSL 1.1.1 及 3.x。项目也包含面向 ARM 客户端交叉编译使用的 OpenSSL 兼容逻辑。

Debian/Ubuntu：

```bash
sudo apt update
sudo apt install build-essential pkg-config libssl-dev openssl
```

Red Hat、Rocky Linux、AlmaLinux 或 Fedora：

```bash
sudo dnf install gcc make pkgconf-pkg-config openssl-devel openssl
```

## 构建

生成证书并构建本机服务端/客户端：

```bash
bash scripts/generate_certs.sh certs
make all
```

`make all` 会生成：

```text
bin/tls_server
bin/tls_client
```

如果 `certs/` 中还没有测试证书，构建过程会自动生成。

可用的 Make 目标：

```bash
make help
make all
make arm-client
make certs
make test
make clean
```

其中：

- `make all`：构建本机服务端和本机客户端。
- `make arm-client`：交叉编译 ARM 客户端，输出仍为 `bin/tls_client`。
- `make certs`：生成 demo 证书。
- `make test`：运行自动化测试。
- `make clean`：删除 `bin/` 编译产物。

注意：`make all` 会覆盖 `bin/tls_client` 为本机版本；如果最终要部署 ARM 版本，请最后执行 `make arm-client`。

## 本机运行

在第一个终端启动服务端：

```bash
./bin/tls_server
```

服务端默认监听 `18443` 端口，并输出：

```text
Listening on port 18443
```

在第二个终端启动客户端。

当前客户端默认连接地址是 ARM 设备部署场景使用的：

```text
192.168.168.142
```

因此本机测试时建议显式指定 `localhost`：

```bash
./bin/tls_client --host localhost --verify-name localhost 'hello TLS 1.2'
```

连接成功后，双方都会显示协商协议、密码套件和对端证书主题。客户端输出应包含：

```text
Protocol: TLSv1.2
Echo: hello TLS 1.2
```

服务端输出应包含：

```text
Protocol: TLSv1.2
Received: hello TLS 1.2
```

每次客户端连接只发送一条 1 至 4096 字节的消息。服务端处理并回显后关闭本次 TLS 连接，然后继续等待下一次连接。

## 命令行参数

服务端：

```text
tls_server [--port PORT] [--ca FILE] [--cert FILE] [--key FILE]
```

- `--port PORT`：监听端口，默认 `18443`。
- `--ca FILE`：用于验证客户端证书的 CA，默认 `certs/ca.crt`。
- `--cert FILE`：服务端证书，默认 `certs/server.crt`。
- `--key FILE`：服务端私钥，默认 `certs/server.key`。
- `--help`：显示帮助并退出。

客户端：

```text
tls_client [--host HOST] [--port PORT] [--verify-name NAME] [--ca FILE] [--cert FILE] [--key FILE] MESSAGE
```

- `--host HOST`：连接地址，默认 `192.168.168.142`。
- `--port PORT`：服务端端口，默认 `18443`。
- `--verify-name NAME`：服务端证书主机名或 IP 校验值，默认 `192.168.168.142`。
- `--ca FILE`：用于验证服务端证书的 CA，默认 `certs/ca.crt`。
- `--cert FILE`：客户端证书，默认 `certs/client.crt`。
- `--key FILE`：客户端私钥，默认 `certs/client.key`。
- `--help`：显示帮助并退出。
- `MESSAGE`：必需的单个字符串参数，长度必须为 1 至 4096 字节。

## 测试证书

`scripts/generate_certs.sh` 会在 `certs/` 中生成：

- `ca.crt`、`ca.key`：测试根 CA 证书和私钥。
- `server.crt`、`server.key`：服务端证书和私钥；证书用途为 `serverAuth`，SAN 包含 `localhost`、`127.0.0.1` 和 `192.168.168.142`。
- `client.crt`、`client.key`：客户端证书和私钥；证书用途为 `clientAuth`。

这些证书和私钥仅供 demo 使用，不应部署到生产环境，也不应提交到版本库。生产环境应使用受控 PKI 签发、分发和轮换证书，并妥善保护私钥。

## ARM 客户端交叉编译

当前 Makefile 已配置 ARM 客户端交叉编译目标：

```text
交叉编译器：/opt/arm-ca53-linux-gnueabihf-8.4/bin/arm-ca53-linux-gnueabihf-gcc
OpenSSL 头文件目录：openssl
OpenSSL 动态库目录：lib
输出路径：bin/tls_client
```

执行：

```bash
make arm-client
```

生成的 `bin/tls_client` 是 ARM 32 位可执行文件，动态依赖：

```text
libssl.so.1.1
libcrypto.so.1.1
libc.so.6
```

可用以下命令确认：

```bash
file bin/tls_client
readelf -d bin/tls_client | grep NEEDED
```

## ARM 端部署目录

ARM 设备运行需要的文件已整理到：

```text
arm_tls/
```

核心文件：

```text
arm_tls/
├── bin/
│   └── tls_client
├── certs/
│   ├── ca.crt
│   ├── client.crt
│   └── client.key
└── lib/
    ├── libssl.so
    ├── libssl.so.1.1
    ├── libcrypto.so
    └── libcrypto.so.1.1
```

将 `arm_tls` 拷贝到 ARM 设备后运行：

```bash
cd arm_tls
export LD_LIBRARY_PATH="$PWD/lib"
./bin/tls_client "hello from arm"
```

如果设备系统库路径中已经有兼容的 `libssl.so.1.1` 和 `libcrypto.so.1.1`，也可以不设置 `LD_LIBRARY_PATH`。

## 服务端部署运行

服务端通常运行在开发机或服务器上。

编译服务端：

```bash
make all
```

启动服务端：

```bash
./bin/tls_server --port 18443
```

完整参数形式：

```bash
./bin/tls_server \
  --port 18443 \
  --ca certs/ca.crt \
  --cert certs/server.crt \
  --key certs/server.key
```

需要确保服务端机器放通 TCP 端口：

```text
18443
```

如果 ARM 客户端使用默认配置连接，服务端 IP 应为：

```text
192.168.168.142
```

否则客户端需要通过参数指定实际地址，并且服务端证书 SAN 也要包含该地址：

```bash
./bin/tls_client --host 实际服务端IP --verify-name 实际服务端IP "hello"
```

## 自动化测试

```bash
make test
```

测试包括证书用途与权限检查、消息长度边界、TLS 上下文、完整 TLS 读写以及端到端回显。端到端负面场景还会确认：

- 未提供客户端证书时，服务端拒绝握手。
- 客户端使用错误 CA 时，服务端证书验证失败。
- 服务端证书名称不匹配时，客户端拒绝连接。
- 超过 4096 字节的消息在网络连接前被拒绝。
- 成功连接只协商 `TLSv1.2`。

## 关键实现说明

### `SSL_CTX_set_verify`

`SSL_CTX_set_verify(ctx, verify_mode, NULL)` 用于设置证书验证策略。

服务端传入：

```c
SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
```

表示服务端要求并验证客户端证书。

客户端传入：

```c
SSL_VERIFY_PEER
```

表示客户端验证服务端证书。

去掉该配置会削弱或破坏 mTLS 身份验证。

### `SSL_set_fd` 和 `SSL_connect`

- `SSL_set_fd(ssl, socket_fd)`：把 TLS 对象绑定到底层 TCP socket。
- `SSL_connect(ssl)`：作为客户端在该 socket 上发起 TLS 握手。

握手完成后，才可以安全调用 `SSL_read`、`SSL_write` 或本项目封装的 `tls_send_message`、`tls_receive_message`。

### 客户端发送和接收逻辑

客户端发送后再接收服务端回显：

```c
if (!tls_send_message(...)) {
    /* 发送失败 */
} else if (!tls_receive_message(...)) {
    /* 发送成功但接收失败 */
} else {
    /* 发送和接收都成功 */
}
```

`tls_receive_message()` 只有在 `tls_send_message()` 成功时才会执行。

## 常见问题

- `pkg-config` 找不到 OpenSSL：安装对应发行版的 OpenSSL 开发包，或确认编译器能使用 `-lssl -lcrypto`。
- 证书文件不存在：运行 `make certs` 或 `bash scripts/generate_certs.sh certs`。
- `certificate verify failed`：确认双方加载的 CA 能验证对方证书，且证书用途与服务端/客户端角色一致。
- `hostname mismatch` 或证书名称不匹配：确认 `--verify-name` 对应的 IP/域名在服务端证书 SAN 中。
- 端口被占用：为服务端和客户端同时指定其他端口，例如 `--port 19443`。
- ARM 设备找不到 `libssl.so.1.1` 或 `libcrypto.so.1.1`：设置 `LD_LIBRARY_PATH=$PWD/lib`，或将动态库放入设备系统库目录。
- ARM 设备时间错误：TLS 证书依赖系统时间，需要先完成 NTP/SNTP 或其他时间同步。
