# TLS 1.2 mTLS Demo 与 ARM 客户端部署整理

## 1. Demo 主要功能

本项目是一个基于 C/OpenSSL 的 TLS 1.2 双向认证 mTLS 通信 demo。

主要功能：

- 生成本地测试用 CA、服务端证书、客户端证书。
- 启动 TLS 服务端，要求客户端提供证书。
- 启动 TLS 客户端，验证服务端证书和主机名/IP。
- 客户端发送一条消息，服务端收到后原样回显。
- 使用 4 字节网络序长度前缀封装业务消息。
- 提供证书、TLS 上下文、TLS I/O 和集成测试。

核心文件：

```text
src/server.c           服务端入口
src/client.c           客户端入口
src/tls_common.c       TLS 公共封装
include/tls_common.h   TLS 公共接口
scripts/generate_certs.sh 证书生成脚本
Makefile               构建入口
```

## 2. 当前 Demo 的限制

该 demo 适合学习和验证 mTLS 流程，但不是生产级服务。

主要限制：

- 服务端单线程阻塞处理。
- 每个连接只处理一条消息。
- 没有连接、握手、读写超时。
- 没有证书吊销检查，例如 CRL/OCSP。
- 默认固定 TLS 1.2。
- 消息协议较简单，仅包含长度和载荷。
- 日志和错误码较基础。

## 3. certs 目录文件说明

`certs` 目录用于保存 TLS/mTLS 证书材料。

| 文件 | 用途 |
|---|---|
| `ca.crt` | CA 证书，客户端和服务端用它验证对方证书。 |
| `ca.key` | CA 私钥，用于签发服务端和客户端证书。真实环境必须严格保密。 |
| `server.crt` | 服务端证书，服务端发送给客户端验证服务端身份。 |
| `server.key` | 服务端私钥，服务端 TLS 握手时使用。 |
| `client.crt` | 客户端证书，客户端发送给服务端验证客户端身份。 |
| `client.key` | 客户端私钥，客户端 TLS 握手时使用。 |

运行时：

服务端使用：

```text
certs/ca.crt
certs/server.crt
certs/server.key
```

客户端使用：

```text
certs/ca.crt
certs/client.crt
certs/client.key
```

生成证书时还会临时出现：

```text
server.csr
client.csr
server.ext
client.ext
ca.srl
```

这些文件在脚本结束时会被删除。

## 4. 单向认证需要哪些文件

如果只做单向 TLS 认证，客户端验证服务端，服务端不验证客户端，则最少需要：

```text
certs/ca.crt
certs/server.crt
certs/server.key
```

不需要：

```text
client.crt
client.key
```

但当前 demo 是双向认证，服务端会强制要求客户端证书。如果只删除客户端证书而不改代码，握手会失败。

## 5. 双向认证时 CA 是否可以不同

可以不同。

双向认证里有两个独立验证方向：

```text
客户端验证服务端证书：客户端需要信任签发 server.crt 的 CA。
服务端验证客户端证书：服务端需要信任签发 client.crt 的 CA。
```

因此：

```text
客户端使用的 ca.crt 可以是 server-ca.crt
服务端使用的 ca.crt 可以是 client-ca.crt
```

只要各自能验证对方证书即可。

当前 demo 为了简单，服务端和客户端默认都使用同一个：

```text
certs/ca.crt
```

## 6. CA 的含义

CA 全称是：

```text
Certificate Authority
```

中文一般叫：

```text
证书颁发机构
```

CA 的作用是签发证书，并让通信双方根据受信任的 CA 证书验证对方证书是否可信。

在本 demo 中：

```text
ca.crt  CA 证书，用于验证 server.crt/client.crt
ca.key  CA 私钥，用于签发 server.crt/client.crt
```

技术上任何人都可以生成自己的 CA，但只有被通信双方信任的 CA 才有意义。

## 7. TLS/mTLS 通信流程

TLS 通信大致分为两个阶段：

```text
1. TLS 握手阶段：身份验证和密钥协商
2. 加密通信阶段：使用协商出的对称密钥传输业务数据
```

mTLS 双向认证流程：

```text
客户端                                      服务端
  |                                           |
  | -------- TCP connect -------------------> |
  |                                           |
  | -------- ClientHello -------------------> |
  |          支持版本/加密套件/SNI             |
  |                                           |
  | <------- ServerHello -------------------- |
  |          选择版本/加密套件                 |
  |                                           |
  | <------- server.crt --------------------- |
  |                                           |
  | 客户端验证 server.crt                     |
  | - 是否可信 CA 签发                         |
  | - 主机名/IP 是否匹配                       |
  | - 是否过期                                |
  |                                           |
  | <------- CertificateRequest ------------- |
  |          服务端要求客户端证书              |
  |                                           |
  | -------- client.crt --------------------> |
  |          客户端证明身份                    |
  |                                           |
  |                                           | 服务端验证 client.crt
  |                                           | - 是否可信 CA 签发
  |                                           | - 是否允许 clientAuth
  |                                           | - 是否过期
  |                                           |
  | <-------> 密钥交换 / Finished <---------> |
  |                                           |
  | ======== 加密通道建立 =================== |
  |                                           |
  | -------- 加密业务数据 -------------------> |
  |                                           |
  | <------- 加密业务数据 -------------------- |
```

当前 demo 的业务数据格式：

```text
4 字节网络序长度 + 消息内容
```

## 8. ARM 客户端部署需求

当前 ARM 客户端目标设备信息：

```text
交叉编译器：/opt/arm-ca53-linux-gnueabihf-8.4/bin/arm-ca53-linux-gnueabihf-gcc
OpenSSL 头文件目录：openssl
OpenSSL 动态库目录：lib
服务器地址：192.168.168.142
输出路径：bin/tls_client
```

当前已调整：

- 客户端默认连接地址：`192.168.168.142`
- 客户端默认校验名称：`192.168.168.142`
- 服务端证书 SAN 包含：`IP:192.168.168.142`
- 新增 Makefile 目标：`arm-client`
- `arm-client` 使用项目内 `openssl` 头文件和 `lib` 目录动态库。

ARM 编译命令：

```bash
make arm-client
```

生成：

```text
bin/tls_client
```

已验证生成的客户端是：

```text
ELF 32-bit LSB executable, ARM, EABI5
```

动态依赖：

```text
libssl.so.1.1
libcrypto.so.1.1
libc.so.6
```

## 9. arm_tls 目录内容

已将 ARM 端运行需要的文件整理到：

```text
arm_tls/
```

主要内容：

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

其中：

- `arm_tls/bin/tls_client` 是 ARM 客户端程序。
- `arm_tls/lib/*.so*` 是 ARM OpenSSL 动态库。
- `arm_tls/certs/client.key` 权限已设置为 `600`。

注意：当前 `arm_tls` 目录里可能还保留一些额外文件，例如服务端证书、服务端私钥、测试二进制等。真正部署客户端时，核心需要的是上面列出的客户端文件。

## 10. 服务端运行方式

服务端运行在开发机或服务器上。

编译服务端：

```bash
make all
```

或只编译服务端：

```bash
make bin/tls_server
```

生成证书：

```bash
make certs
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

启动成功后输出：

```text
Listening on port 18443
```

服务端需要放通 TCP 端口：

```text
18443
```

## 11. ARM 设备运行方式

把 `arm_tls` 目录拷贝到 ARM 设备后执行：

```bash
cd arm_tls
export LD_LIBRARY_PATH="$PWD/lib"
./bin/tls_client "hello"
```

如果连接成功，客户端会输出类似：

```text
Protocol: TLSv1.2
Cipher: ...
Peer subject: /CN=localhost
Echo: hello
```

服务端会输出类似：

```text
Protocol: TLSv1.2
Cipher: ...
Peer subject: /CN=demo-client
Received: hello
```

## 12. 常见问题

### 12.1 找不到 libssl.so.1.1 或 libcrypto.so.1.1

运行前设置：

```bash
export LD_LIBRARY_PATH="$PWD/lib"
```

或者把库放入设备系统库路径，例如：

```text
/lib
/usr/lib
```

### 12.2 证书名称不匹配

如果客户端报服务端证书名称不匹配，需要确认：

- 客户端 `--verify-name` 使用的 IP/域名是否正确。
- 服务端证书 SAN 是否包含该 IP/域名。

当前默认为：

```text
192.168.168.142
```

### 12.3 设备时间错误导致证书失败

TLS 证书校验依赖系统时间。

嵌入式设备如果没有 RTC，开机时间错误可能导致：

```text
certificate is not yet valid
certificate has expired
```

需要先做 NTP/SNTP 或其他时间同步。

### 12.4 make all 会覆盖 ARM 客户端

`make all` 会重新生成本机版本：

```text
bin/tls_client
```

如果最终要部署 ARM 版本，请最后执行：

```bash
make arm-client
```

然后再复制或打包 `arm_tls`。

## 13. 推荐部署文件

ARM 客户端最小部署文件：

```text
arm_tls/bin/tls_client
arm_tls/certs/ca.crt
arm_tls/certs/client.crt
arm_tls/certs/client.key
arm_tls/lib/libssl.so.1.1
arm_tls/lib/libcrypto.so.1.1
```

推荐运行脚本示例：

```bash
#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib"
exec "$DIR/bin/tls_client" "$@"
```

保存为 `arm_tls/run_client.sh` 后，可在设备上执行：

```bash
./run_client.sh "hello"
```

## 14. 代码关键点补充说明

### 14.1 `SSL_CTX_set_verify` 的作用

代码位置：`src/tls_common.c` 中 `configure_context()`。

```c
SSL_CTX_set_verify(ctx, verify_mode, NULL);
```

这行用于设置当前 TLS 上下文的证书验证策略。

服务端创建上下文时传入：

```c
SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
```

含义：

```text
服务端要验证客户端证书，并且客户端不提供证书时握手失败。
```

客户端创建上下文时传入：

```c
SSL_VERIFY_PEER
```

含义：

```text
客户端要验证服务端证书。
```

如果完全去掉这行，会破坏当前 demo 的双向认证能力：

- 服务端可能不再强制客户端证书。
- 客户端可能不再验证服务端证书链。
- `SSL_get_verify_result()` 只能读取验证结果，不能替代 `SSL_CTX_set_verify()` 开启验证策略。

### 14.2 如果只在客户端去掉 `SSL_CTX_set_verify`

如果只影响 `tls_create_client_context()`，即客户端不设置 `SSL_VERIFY_PEER`，后果是：

```text
服务端仍可能验证客户端证书，但客户端不再可靠验证服务端证书链。
```

即使仍然加载：

```c
SSL_CTX_load_verify_locations(ctx, ca_file, NULL)
```

也只是告诉 OpenSSL 可信 CA 在哪里，不等于强制开启服务端证书验证。

风险：

- 错误 CA 可能不再导致握手失败。
- 自签或伪造的服务端证书可能被接受。
- 手动主机名/IP 校验不能替代 CA 链验证，因为攻击者也可以伪造一个 SAN 匹配的证书。

完整客户端验证应同时满足：

```text
1. server.crt 由可信 CA 签发。
2. server.crt 的 SAN/CN 匹配客户端要连接的主机名或 IP。
```

### 14.3 `SSL_set_fd` 做了什么

客户端代码中：

```c
SSL_set_fd(ssl, socket_fd)
```

作用是把 OpenSSL 的 `SSL *ssl` 对象绑定到底层 TCP socket 文件描述符 `socket_fd`。

可以理解为：

```text
socket_fd 是已经连上的 TCP 通道。
ssl 是 TLS 协议状态机。
SSL_set_fd 是把 TLS 状态机挂到 TCP 通道上。
```

调用后，下面这些函数都会通过该 socket 进行网络读写：

```c
SSL_connect(ssl)
SSL_read(ssl, ...)
SSL_write(ssl, ...)
SSL_shutdown(ssl)
```

如果不调用 `SSL_set_fd()`，OpenSSL 不知道通过哪个 socket 通信，后续 `SSL_connect()` 会失败。

### 14.4 `SSL_connect` 做了什么

客户端代码中：

```c
SSL_connect(ssl)
```

作用是作为 TLS 客户端发起并完成 TLS 握手。

它会在前面绑定的 `socket_fd` 上执行：

```text
1. 发送 ClientHello。
2. 接收 ServerHello。
3. 接收服务端证书 server.crt。
4. 验证服务端证书链。
5. 根据服务端要求发送客户端证书 client.crt。
6. 使用 client.key 证明客户端拥有私钥。
7. 协商加密套件和会话密钥。
8. 完成 Finished 消息。
```

成功后，TLS 加密通道建立，后续才能安全调用：

```c
tls_send_message(...)
tls_receive_message(...)
```

简单对比：

| 函数 | 作用 | 是否发起 TLS 握手 |
|---|---|---|
| `SSL_set_fd` | 绑定底层 TCP socket | 否 |
| `SSL_connect` | 作为客户端执行 TLS 握手 | 是 |

### 14.5 客户端发送和接收分支逻辑

客户端代码中：

```c
if (!tls_send_message(ssl, (const unsigned char *)message, message_length)) {
    tls_print_errors("send TLS message");
} else if (!tls_receive_message(ssl, echo, TLS_MAX_MESSAGE_SIZE, &echo_length)) {
    tls_print_errors("receive TLS message");
} else {
    echo[echo_length] = '\0';
    printf("Echo: %s\n", echo);
    status = EXIT_SUCCESS;
}
```

这里 `tls_receive_message()` 是可以执行到的，但前提是：

```text
tls_send_message() 成功。
```

执行顺序等价于：

```c
if (!tls_send_message(...)) {
    /* 发送失败 */
} else {
    if (!tls_receive_message(...)) {
        /* 接收失败 */
    } else {
        /* 接收成功 */
    }
}
```

也就是说，逻辑是：

```text
先发送消息
  ├── 发送失败：打印 send TLS message 错误
  └── 发送成功：继续接收服务端回显
        ├── 接收失败：打印 receive TLS message 错误
        └── 接收成功：打印 Echo 并设置成功状态
```

`else if` 的条件表达式本身会被求值，因此在发送成功后会调用 `tls_receive_message()`。
