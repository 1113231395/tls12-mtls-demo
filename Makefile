# 编译器和基础告警选项；默认把告警当作错误，避免示例代码退化。
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror

# ARM 设备客户端使用指定交叉编译器，并动态链接目标系统上的 OpenSSL 库。
ARM_CC ?= /opt/arm-ca53-linux-gnueabihf-8.4/bin/arm-ca53-linux-gnueabihf-gcc
ARM_OPENSSL_CFLAGS ?= -I. -DTLS_HAVE_X509_CHECK_HOST
ARM_OPENSSL_LIBS ?= -Llib -lssl -lcrypto

# 优先使用 pkg-config 获取 OpenSSL 编译和链接参数。
OPENSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
OPENSSL_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)
ifeq ($(strip $(OPENSSL_LIBS)),)
# 没有 pkg-config 时退回到常见的 OpenSSL 链接库名。
OPENSSL_LIBS := -lssl -lcrypto
endif

# 这些目标不对应同名文件，始终作为命令入口执行。
.PHONY: all arm-client certs test test-certs test-unit test-context test-io integration-test clean help

# 默认目标构建服务端和客户端两个 demo 程序。
all: bin/tls_server bin/tls_client

# 只为 ARM 嵌入式设备交叉编译客户端，输出路径仍为 bin/tls_client。
arm-client: certs/ca.crt | bin
	$(ARM_CC) $(CFLAGS) $(ARM_OPENSSL_CFLAGS) -Iinclude src/client.c src/tls_common.c $(ARM_OPENSSL_LIBS) -o bin/tls_client

# 完整测试先确保证书可生成，再依次运行单元、上下文、I/O 和集成测试。
test: test-certs test-unit test-context test-io integration-test

# 消息长度校验不依赖真实 TLS 握手，作为轻量单元测试单独运行。
test-unit: bin/test_message_validation
	./bin/test_message_validation

# 证书目标由脚本生成 CA、服务端证书和客户端证书。
certs: certs/ca.crt

certs/ca.crt: scripts/generate_certs.sh
	bash scripts/generate_certs.sh certs

# 证书测试验证用途扩展、SAN 和私钥权限。
test-certs:
	bash tests/test_certs.sh

# 上下文测试验证 TLS 版本限制和证书加载失败路径。
test-context: bin/test_tls_context
	./bin/test_tls_context

# I/O 测试使用本地 socketpair 模拟一组 TLS 双端。
test-io: bin/test_tls_io
	./bin/test_tls_io

# 集成测试启动真实服务端和客户端，覆盖成功回显与预期失败场景。
integration-test: bin/tls_server bin/tls_client certs/ca.crt
	bash tests/test_integration.sh

# 各测试二进制都复用公共 TLS 实现，确保测试覆盖生产路径。
bin/test_message_validation: tests/test_message_validation.c src/tls_common.c include/tls_common.h | bin
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -Iinclude tests/test_message_validation.c src/tls_common.c $(OPENSSL_LIBS) -o $@

bin/test_tls_context: tests/test_tls_context.c src/tls_common.c include/tls_common.h certs/ca.crt | bin
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -Iinclude tests/test_tls_context.c src/tls_common.c $(OPENSSL_LIBS) -o $@

bin/test_tls_io: tests/test_tls_io.c src/tls_common.c include/tls_common.h certs/ca.crt | bin
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -Iinclude tests/test_tls_io.c src/tls_common.c $(OPENSSL_LIBS) -o $@

# demo 服务端和客户端都链接同一个公共 TLS 工具模块。
bin/tls_server: src/server.c src/tls_common.c include/tls_common.h certs/ca.crt | bin
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -Iinclude src/server.c src/tls_common.c $(OPENSSL_LIBS) -o $@

bin/tls_client: src/client.c src/tls_common.c include/tls_common.h certs/ca.crt | bin
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -Iinclude src/client.c src/tls_common.c $(OPENSSL_LIBS) -o $@

# 所有编译产物集中放到 bin 目录，便于清理和 .gitignore 排除。
bin:
	mkdir -p $@

# 只删除编译产物，保留证书以便重复运行 demo。
clean:
	rm -rf bin

# 帮助目标列出最常用入口。
help:
	@printf '%s\n' \
	  'all         Build the TLS server and client (default)' \
	  'arm-client  Cross-compile the ARM client to bin/tls_client' \
	  'certs       Generate the demo CA, server, and client certificates' \
	  'test        Run certificate, unit, TLS context, TLS I/O, and integration tests' \
	  'clean       Remove compiled binaries'
