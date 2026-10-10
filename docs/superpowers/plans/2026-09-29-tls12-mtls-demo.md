# TLS 1.2 mTLS C Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建一个 Linux 纯 C Demo，使用 OpenSSL 强制执行双向 TLS 1.2 握手，并可靠地完成一条长度前缀字符串的发送和回显。

**Architecture:** 项目由证书生成脚本、共享 TLS/消息辅助层、阻塞式服务端和阻塞式客户端组成。共享层统一负责 TLS 1.2 限制、证书验证、OpenSSL 错误输出和完整消息读写；客户端与服务端只编排各自连接流程。

**Tech Stack:** C11、POSIX sockets、OpenSSL 1.1.1+、GNU Make、Bash、OpenSSL CLI

**Spec:** `docs/superpowers/specs/2026-09-29-tls12-mtls-demo-design.md`

## Global Constraints

- 目标平台是 Linux，源代码使用纯 C11 和 POSIX API。
- OpenSSL API 必须兼容 1.1.1 及 3.x。
- TLS 最低及最高版本都必须是 `TLS1_2_VERSION`。
- 服务端必须使用 `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT`。
- 客户端必须验证服务端证书链和 `localhost` 主机名，不得提供关闭验证的选项。
- 消息格式是 4 字节网络字节序长度加原始载荷，载荷长度范围为 1 到 4096 字节。
- 测试私钥和证书位于 `certs/`，不得提交到 Git。
- 编译必须启用 `-std=c11 -Wall -Wextra -Wpedantic -Werror`。

---

### Task 1: Test PKI Generator

**Files:**
- Create: `.gitignore`
- Create: `scripts/generate_certs.sh`
- Create: `tests/test_certs.sh`

**Interfaces:**
- Consumes: OpenSSL command line tool.
- Produces: `generate_certs.sh [OUTPUT_DIR]`, creating `ca.crt`, `ca.key`, `server.crt`, `server.key`, `client.crt`, and `client.key`.

- [ ] **Step 1: Write the failing certificate test**

Create `tests/test_certs.sh` with an isolated output directory and exact certificate assertions:

```bash
#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf -- "$tmp_dir"' EXIT

"$root_dir/scripts/generate_certs.sh" "$tmp_dir"
openssl verify -CAfile "$tmp_dir/ca.crt" -purpose sslserver "$tmp_dir/server.crt"
openssl verify -CAfile "$tmp_dir/ca.crt" -purpose sslclient "$tmp_dir/client.crt"
openssl x509 -in "$tmp_dir/server.crt" -noout -ext subjectAltName |
  grep -F 'DNS:localhost, IP Address:127.0.0.1'
openssl x509 -in "$tmp_dir/server.crt" -noout -ext extendedKeyUsage |
  grep -F 'TLS Web Server Authentication'
openssl x509 -in "$tmp_dir/client.crt" -noout -ext extendedKeyUsage |
  grep -F 'TLS Web Client Authentication'
test "$(stat -c '%a' "$tmp_dir/server.key")" = 600
test "$(stat -c '%a' "$tmp_dir/client.key")" = 600
```

- [ ] **Step 2: Run the test and verify the expected failure**

Run: `bash tests/test_certs.sh`

Expected: FAIL because `scripts/generate_certs.sh` does not exist.

- [ ] **Step 3: Implement the generator**

Create `scripts/generate_certs.sh`. It must accept `${1:-certs}`, create a self-signed RSA-2048 CA, and sign two RSA-2048 leaf certificates. Generate temporary OpenSSL extension files inside the output directory with these exact constraints:

```ini
# server.ext
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost,IP:127.0.0.1

# client.ext
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=clientAuth
```

Use subjects `/CN=TLS Demo Root CA`, `/CN=localhost`, and `/CN=demo-client`; issue leaves for 365 days, remove CSRs, serial files, and extension files after signing, and run `chmod 600` on all private keys. Add `/certs/` and `/bin/` to `.gitignore`.

- [ ] **Step 4: Run the certificate test**

Run: `bash tests/test_certs.sh`

Expected: both `openssl verify` calls print `OK`; all `grep` and permission checks pass.

- [ ] **Step 5: Commit**

```bash
git add .gitignore scripts/generate_certs.sh tests/test_certs.sh
git commit -m "feat: generate mTLS test certificates"
```

---

### Task 2: Framed Message Validation and Build Harness

**Files:**
- Create: `Makefile`
- Create: `include/tls_common.h`
- Create: `src/tls_common.c`
- Create: `tests/test_message_validation.c`

**Interfaces:**
- Consumes: no earlier C interfaces.
- Produces: `#define TLS_MAX_MESSAGE_SIZE 4096U` and `int tls_validate_message_length(uint32_t length)`.

- [ ] **Step 1: Write the failing unit test**

```c
#include "tls_common.h"

#include <assert.h>

int main(void)
{
    assert(tls_validate_message_length(0U) == 0);
    assert(tls_validate_message_length(1U) == 1);
    assert(tls_validate_message_length(TLS_MAX_MESSAGE_SIZE) == 1);
    assert(tls_validate_message_length(TLS_MAX_MESSAGE_SIZE + 1U) == 0);
    return 0;
}
```

Create a Makefile target `bin/test_message_validation` compiling `tests/test_message_validation.c` with `src/tls_common.c`, the global warning flags, `-Iinclude`, and OpenSSL flags from `pkg-config openssl` with a `-lssl -lcrypto` fallback. Add `make test-unit` to execute this binary.

- [ ] **Step 2: Run the test and verify the expected failure**

Run: `make test-unit`

Expected: FAIL because `tls_common.h` and `tls_validate_message_length` do not exist.

- [ ] **Step 3: Implement message length validation**

Declare the constant and function in `include/tls_common.h`; implement exactly this rule in `src/tls_common.c`:

```c
int tls_validate_message_length(uint32_t length)
{
    return length > 0U && length <= TLS_MAX_MESSAGE_SIZE;
}
```

Make all build targets create `bin/` with an order-only prerequisite.

- [ ] **Step 4: Run the unit test**

Run: `make clean && make test-unit`

Expected: compilation has zero warnings and `bin/test_message_validation` exits 0.

- [ ] **Step 5: Commit**

```bash
git add Makefile include/tls_common.h src/tls_common.c tests/test_message_validation.c
git commit -m "feat: add framed message validation"
```

---

### Task 3: Verified TLS 1.2 Contexts

**Files:**
- Modify: `include/tls_common.h`
- Modify: `src/tls_common.c`
- Create: `tests/test_tls_context.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: generated files under `certs/`.
- Produces: `tls_create_server_context`, `tls_create_client_context`, `tls_configure_hostname`, `tls_print_errors`, and `tls_print_session_info`.

Add these exact declarations:

```c
SSL_CTX *tls_create_server_context(const char *ca_file,
                                   const char *cert_file,
                                   const char *key_file);
SSL_CTX *tls_create_client_context(const char *ca_file,
                                   const char *cert_file,
                                   const char *key_file);
int tls_configure_hostname(SSL *ssl, const char *hostname);
void tls_print_errors(const char *operation);
void tls_print_session_info(const SSL *ssl, FILE *stream);
```

- [ ] **Step 1: Write the failing context test**

Create `tests/test_tls_context.c` that creates both contexts from `certs/`, asserts they are non-null, checks both min/max protocol versions equal `TLS1_2_VERSION`, checks the server verify mode contains both required flags, and asserts a context call using `certs/missing.crt` returns `NULL`:

```c
server_ctx = tls_create_server_context("certs/ca.crt", "certs/server.crt",
                                       "certs/server.key");
client_ctx = tls_create_client_context("certs/ca.crt", "certs/client.crt",
                                       "certs/client.key");
assert(server_ctx != NULL);
assert(client_ctx != NULL);
assert(SSL_CTX_get_min_proto_version(server_ctx) == TLS1_2_VERSION);
assert(SSL_CTX_get_max_proto_version(server_ctx) == TLS1_2_VERSION);
assert((SSL_CTX_get_verify_mode(server_ctx) & SSL_VERIFY_PEER) != 0);
assert((SSL_CTX_get_verify_mode(server_ctx) & SSL_VERIFY_FAIL_IF_NO_PEER_CERT) != 0);
assert(tls_create_client_context("certs/missing.crt", "certs/client.crt",
                                 "certs/client.key") == NULL);
```

Make the target depend on a `certs/ca.crt` target that invokes `bash scripts/generate_certs.sh certs`.

- [ ] **Step 2: Run the context test and verify the expected failure**

Run: `make bin/test_tls_context && ./bin/test_tls_context`

Expected: FAIL at link time because the context functions are undefined.

- [ ] **Step 3: Implement strict context setup**

Use `TLS_server_method()` and `TLS_client_method()`. In a private `configure_context` helper:

```c
if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1 ||
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION) != 1 ||
    SSL_CTX_load_verify_locations(ctx, ca_file, NULL) != 1 ||
    SSL_CTX_use_certificate_chain_file(ctx, cert_file) != 1 ||
    SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1 ||
    SSL_CTX_check_private_key(ctx) != 1) {
    tls_print_errors("configure TLS context");
    SSL_CTX_free(ctx);
    return NULL;
}
```

Set client verification to `SSL_VERIFY_PEER`; set server verification to both required flags. `tls_configure_hostname` must call `SSL_set_tlsext_host_name` and `SSL_set1_host`. `tls_print_session_info` must print `SSL_get_version`, `SSL_get_cipher`, and the peer subject plus `X509_NAME_oneline`, then free the certificate. Obtain the owned certificate reference compatibly:

```c
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
peer = SSL_get1_peer_certificate(ssl);
#else
peer = SSL_get_peer_certificate(ssl);
#endif
```

- [ ] **Step 4: Run the certificate and context tests**

Run: `make clean && make test-certs test-context`

Expected: certificate checks pass; context test exits 0. The intentional missing-CA call may print an OpenSSL error stack but must not leak resources.

- [ ] **Step 5: Commit**

```bash
git add Makefile include/tls_common.h src/tls_common.c tests/test_tls_context.c
git commit -m "feat: configure verified TLS 1.2 contexts"
```

---

### Task 4: Complete TLS Message I/O

**Files:**
- Modify: `include/tls_common.h`
- Modify: `src/tls_common.c`
- Create: `tests/test_tls_io.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: verified client/server contexts from Task 3.
- Produces: complete read/write and framed-message functions used by both executables.

Add these exact declarations:

```c
int tls_write_all(SSL *ssl, const void *buffer, size_t length);
int tls_read_all(SSL *ssl, void *buffer, size_t length);
int tls_send_message(SSL *ssl, const unsigned char *message, uint32_t length);
int tls_receive_message(SSL *ssl, unsigned char *buffer, size_t capacity,
                        uint32_t *length_out);
```

- [ ] **Step 1: Write the failing in-memory mTLS test**

Create `tests/test_tls_io.c`. Use `socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)` and `fork()`. The child builds a server `SSL`, attaches `sockets[1]`, runs `SSL_accept`, receives `"hello tls"`, echoes it, calls `SSL_shutdown`, and exits 0. The parent builds a client `SSL`, configures hostname `localhost`, attaches `sockets[0]`, runs `SSL_connect`, sends `"hello tls"`, receives the echo, asserts exact length/content, waits for a zero child status, and frees all resources.

After the successful exchange, use the already-connected client only for the success path. Validate invalid lengths without I/O:

```c
assert(tls_send_message(client_ssl, (const unsigned char *)"", 0U) == 0);
assert(tls_send_message(client_ssl, oversized, TLS_MAX_MESSAGE_SIZE + 1U) == 0);
```

- [ ] **Step 2: Run the I/O test and verify the expected failure**

Run: `make bin/test_tls_io && ./bin/test_tls_io`

Expected: FAIL at link time because the message I/O functions are undefined.

- [ ] **Step 3: Implement complete SSL reads and writes**

`tls_write_all` and `tls_read_all` must loop until `length` bytes are transferred. Retry `SSL_ERROR_WANT_READ` and `SSL_ERROR_WANT_WRITE`; treat clean close and all other errors as failure. Reject any single `SSL_read`/`SSL_write` chunk larger than `INT_MAX`.

`tls_send_message` validates length, converts it with `htonl`, writes the four-byte header, then writes the payload. `tls_receive_message` reads the header, applies `ntohl`, rejects lengths outside 1..4096 or larger than `capacity`, reads the payload, and assigns `length_out` only on success.

- [ ] **Step 4: Run all unit tests**

Run: `make clean && make test-unit test-context test-io`

Expected: all three C test binaries exit 0 with zero compiler warnings.

- [ ] **Step 5: Commit**

```bash
git add Makefile include/tls_common.h src/tls_common.c tests/test_tls_io.c
git commit -m "feat: add complete TLS message IO"
```

---

### Task 5: Server, Client, and End-to-End Security Tests

**Files:**
- Create: `src/server.c`
- Create: `src/client.c`
- Create: `tests/test_integration.sh`
- Modify: `Makefile`

**Interfaces:**
- Consumes: all functions from `tls_common.h`.
- Produces: `bin/tls_server` and `bin/tls_client` with the command line interfaces defined in the spec.

- [ ] **Step 1: Write the failing integration test**

Create `tests/test_integration.sh` with a fixed overridable port, temporary logs, and an EXIT trap that kills and waits for the server. Add a Makefile target named `integration-test` that depends on `bin/tls_server`, `bin/tls_client`, and `certs/ca.crt`, then runs this script. The script must perform these assertions:

```bash
#!/usr/bin/env bash
set -euo pipefail

port="${TLS_TEST_PORT:-18443}"
tmp_dir="$(mktemp -d)"
server_log="$tmp_dir/server.log"
client_log="$tmp_dir/client.log"
server_pid=''

cleanup() {
  if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
    kill -INT "$server_pid"
    wait "$server_pid" || true
  fi
  rm -rf -- "$tmp_dir"
}
trap cleanup EXIT

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

./bin/tls_client --port "$port" 'hello TLS 1.2' >"$client_log" 2>&1
grep -F 'Protocol: TLSv1.2' "$client_log"
grep -F 'Echo: hello TLS 1.2' "$client_log"
grep -F 'Received: hello TLS 1.2' "$server_log"

if ./bin/tls_client --port "$port" --ca certs/client.crt bad-ca; then
  echo 'client unexpectedly trusted the wrong CA' >&2
  exit 1
fi

if ./bin/tls_client --port "$port" --verify-name invalid.example bad-name; then
  echo 'client unexpectedly accepted the wrong hostname' >&2
  exit 1
fi

oversized="$(head -c 4097 /dev/zero | tr '\0' x)"
if ./bin/tls_client --port "$port" "$oversized"; then
  echo 'client unexpectedly accepted an oversized message' >&2
  exit 1
fi

no_cert_log="$tmp_dir/no-cert.log"
timeout 5 openssl s_client -connect "localhost:$port" -tls1_2 \
  -CAfile certs/ca.crt -verify_hostname localhost < /dev/null \
  >"$no_cert_log" 2>&1 || true
LC_ALL=C grep -E \
  'certificate required|handshake failure|alert bad certificate|peer did not return a certificate' \
  "$no_cert_log"
```

- [ ] **Step 2: Run the integration test and verify the expected failure**

Run: `make integration-test`

Expected: FAIL because `bin/tls_server` and `bin/tls_client` do not exist.

- [ ] **Step 3: Implement the server**

Use `getopt_long` for `--port`, `--ca`, `--cert`, `--key`, and `--help`; reject unknown options and ports outside 1..65535. Create an `AF_INET` listening socket, set `SO_REUSEADDR`, bind to `INADDR_ANY`, and listen with backlog 16.

Install a `SIGINT` handler with `sigaction` and no `SA_RESTART`; the handler only sets a `volatile sig_atomic_t stop_requested` flag. The accept loop must recover from `EINTR`, exiting when that flag is set. For each accepted descriptor, create one `SSL`, call `SSL_accept`, verify `SSL_get_verify_result(ssl) == X509_V_OK`, print session information, receive one message, print `Received: <payload>`, echo it, perform `SSL_shutdown`, and clean up. A failed client handshake must close only that connection. Print `Listening on port <port>` and flush stdout before accepting; also flush after each received-message line so redirected integration-test logs are immediately observable.

- [ ] **Step 4: Implement the client**

Use `getopt_long` for `--host`, `--port`, `--verify-name`, `--ca`, `--cert`, `--key`, and `--help`. Require exactly one message argument and reject its length before network access. Resolve the host with `getaddrinfo`, try each address until `connect` succeeds, create `SSL`, call `tls_configure_hostname`, attach the descriptor, call `SSL_connect`, require `SSL_get_verify_result(ssl) == X509_V_OK`, print session information, send the message, receive the echo, and print `Echo: <payload>`.

Both programs must ignore `SIGPIPE`, report system errors with `strerror(errno)`, report OpenSSL failures with `tls_print_errors`, and free every allocated OpenSSL object, address list, buffer, and descriptor on all exits.

- [ ] **Step 5: Run the end-to-end test**

Run: `make clean && make test`

Expected: certificate tests, all C unit tests, the successful TLSv1.2 echo, and all four negative checks pass.

- [ ] **Step 6: Commit**

```bash
git add Makefile src/server.c src/client.c tests/test_integration.sh
git commit -m "feat: add TLS 1.2 mTLS echo demo"
```

---

### Task 6: Usage Documentation and Final Verification

**Files:**
- Create: `README.md`
- Modify: `Makefile`

**Interfaces:**
- Consumes: final build and test commands.
- Produces: reproducible setup, run, and troubleshooting instructions.

- [ ] **Step 1: Write the README acceptance check**

Before creating the README, run this command and confirm it fails:

```bash
test -f README.md && grep -F 'make test' README.md && grep -F 'TLSv1.2' README.md
```

Expected: FAIL because `README.md` does not exist.

- [ ] **Step 2: Write complete usage documentation**

Document Debian/Ubuntu dependencies (`build-essential`, `pkg-config`, `libssl-dev`, `openssl`), Red Hat-family dependencies (`gcc`, `make`, `pkgconf-pkg-config`, `openssl-devel`, `openssl`), and these exact commands:

```bash
bash scripts/generate_certs.sh certs
make
./bin/tls_server
./bin/tls_client 'hello TLS 1.2'
make test
```

Explain the two terminal workflow, expected `Protocol: TLSv1.2` and echo lines, each command line option, certificate file roles, why the keys are Demo-only, and how the negative tests demonstrate mTLS, CA, and hostname enforcement. Add `make help` listing `all`, `certs`, `test`, and `clean` without deleting source or documentation.

- [ ] **Step 3: Run documentation and clean-build checks**

Run:

```bash
grep -F 'make test' README.md
grep -F 'TLSv1.2' README.md
make clean
make
make test
git diff --check
```

Expected: all commands exit 0; build output contains no warnings; every test passes.

- [ ] **Step 4: Inspect protocol and certificate evidence**

Run:

```bash
openssl x509 -in certs/server.crt -noout -subject -issuer -ext subjectAltName -ext extendedKeyUsage
openssl x509 -in certs/client.crt -noout -subject -issuer -ext extendedKeyUsage
openssl verify -CAfile certs/ca.crt -purpose sslserver certs/server.crt
openssl verify -CAfile certs/ca.crt -purpose sslclient certs/client.crt
```

Expected: the server has `localhost` and `127.0.0.1` SANs plus serverAuth; the client has clientAuth; both verification commands print `OK`.

- [ ] **Step 5: Commit**

```bash
git add README.md Makefile
git commit -m "docs: add TLS demo usage and verification"
```

- [ ] **Step 6: Confirm a clean final state**

Run: `git status --short && git log --oneline --decorate -7`

Expected: no status output; history contains the design, PKI, framing, TLS context, TLS I/O, executable, and documentation commits.
