# 给代码关键段落添加中文注释

## Context

用户希望“给每段代码添加中文注释”，并已明确范围为“全部代码文件”、注释密度为“关键段落”。本项目是 C/OpenSSL/GNU Make 的 TLS 1.2 双向认证 demo，代码量较小但覆盖客户端、服务端、TLS 公共逻辑、证书生成脚本、Makefile 和测试。目标是在不改变行为的前提下，为关键流程、协议边界、证书/TLS 配置、测试意图和构建目标补充清晰的中文注释，同时修复现有乱码中文注释为 UTF-8 可读文本。

## Recommended Approach

1. **只修改手写代码文件，跳过生成产物和文档目录**
   - 修改范围：
     - `src/client.c`
     - `src/server.c`
     - `src/tls_common.c`
     - `include/tls_common.h`
     - `Makefile`
     - `scripts/generate_certs.sh`
     - `tests/test_message_validation.c`
     - `tests/test_tls_context.c`
     - `tests/test_tls_io.c`
     - `tests/test_certs.sh`
     - `tests/test_integration.sh`
   - 排除范围：`bin/`、`certs/`、`.git/`、`.superpowers/`、`docs/superpowers/`。

2. **沿用各语言现有注释风格**
   - C 源码和头文件使用 `/* ... */`，放在结构体、函数、关键流程块或边界检查前。
   - Bash 和 Makefile 使用 `#`，放在变量段、主要步骤、测试断言组和 target 前。
   - 避免每行机械注释，重点解释“为什么这样做”和“这段代码在协议/测试/构建中的角色”。

3. **修复已有乱码中文注释**
   - 当前多处中文注释显示为 mojibake，例如 `src/tls_common.c`、`src/client.c`、`src/server.c`、`include/tls_common.h`、脚本和测试中已有乱码注释。
   - 实施时将这些注释替换为 UTF-8 中文，保持原有含义：TLS 分段读写、长度前缀帧、TLS 1.2 限制、SNI/主机名校验、socketpair 测试、预期失败场景等。

4. **各文件注释重点**
   - `include/tls_common.h`：说明最大消息长度、完整读写封装、长度前缀消息收发、服务端/客户端上下文职责、主机名校验和诊断函数。
   - `src/tls_common.c`：说明完整读写循环、`SSL_ERROR_WANT_READ/WRITE` 重试、4 字节网络序长度前缀、消息长度校验、TLS 1.2 上下文配置、OpenSSL 版本兼容的证书引用释放。
   - `src/client.c`：说明默认参数、端口解析、先校验消息长度再联网、SIGPIPE 忽略、IPv4/IPv6 地址轮询连接、SNI 与证书主机名校验、发送后接收回显。
   - `src/server.c`：说明退出信号处理、监听 socket 设置、每连接一次握手/一条消息/回显、客户端证书校验、主循环接收连接和清理路径。
   - `scripts/generate_certs.sh`：说明输出目录、MSYS 路径兼容、CA/服务端/客户端证书生成、扩展用途、私钥权限、临时签名文件清理。
   - `Makefile`：说明编译参数、OpenSSL flags 探测、主要构建/测试 target、证书依赖、二进制输出目录和清理目标。
   - `tests/*.c` 和 `tests/*.sh`：说明每个测试文件的测试目标、关键断言组、为何使用 `socketpair`/`fork`、为何某些客户端失败是预期结果。

5. **保持代码行为不变**
   - 不改函数签名、命令参数、测试逻辑、构建规则或证书生成参数。
   - 注释修改后避免引入非 ASCII 标点到 shell 命令本身，只在注释文本中使用中文。

## Verification

1. 运行格式/编译相关验证：
   - `make clean`
   - `make all`
2. 运行完整测试：
   - `make test`
3. 如需快速验证注释没有破坏脚本语法，可单独运行：
   - `bash scripts/generate_certs.sh certs`
   - `bash tests/test_certs.sh`
   - `bash tests/test_integration.sh`
4. 检查最终 diff，确认只有注释和乱码注释修复，没有行为性代码变更。
