#include "tls_common.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

/* 负向用例会触发 OpenSSL 错误栈，测试时临时静音 stderr 以保持输出干净。 */
static SSL_CTX *create_client_context_with_stderr_suppressed(
    const char *ca_file, const char *cert_file, const char *key_file)
{
    SSL_CTX *context;
    int null_stderr;
    int saved_stderr;

    fflush(stderr);
    saved_stderr = dup(STDERR_FILENO);
    assert(saved_stderr >= 0);
    null_stderr = open("/dev/null", O_WRONLY);
    assert(null_stderr >= 0);
    assert(dup2(null_stderr, STDERR_FILENO) >= 0);

    context = tls_create_client_context(ca_file, cert_file, key_file);

    fflush(stderr);
    assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
    assert(close(null_stderr) == 0);
    assert(close(saved_stderr) == 0);
    return context;
}

int main(void)
{
    SSL_CTX *client_ctx;
    SSL_CTX *server_ctx;

    /* 有效证书应能分别创建服务端和客户端 TLS 上下文。 */
    server_ctx = tls_create_server_context("certs/ca.crt", "certs/server.crt",
                                           "certs/server.key");
    client_ctx = tls_create_client_context("certs/ca.crt", "certs/client.crt",
                                           "certs/client.key");
    assert(server_ctx != NULL);
    assert(client_ctx != NULL);

    /* demo 明确限制为 TLS 1.2，最大和最小协议版本必须一致。 */
    assert(SSL_CTX_get_min_proto_version(server_ctx) == TLS1_2_VERSION);
    assert(SSL_CTX_get_max_proto_version(server_ctx) == TLS1_2_VERSION);
    assert(SSL_CTX_get_min_proto_version(client_ctx) == TLS1_2_VERSION);
    assert(SSL_CTX_get_max_proto_version(client_ctx) == TLS1_2_VERSION);

    /* 服务端强制要求客户端证书，客户端也必须校验服务端证书。 */
    assert((SSL_CTX_get_verify_mode(server_ctx) & SSL_VERIFY_PEER) != 0);
    assert((SSL_CTX_get_verify_mode(server_ctx) & SSL_VERIFY_FAIL_IF_NO_PEER_CERT) != 0);
    assert((SSL_CTX_get_verify_mode(client_ctx) & SSL_VERIFY_PEER) != 0);

    /* 缺失 CA 文件时上下文创建必须失败，避免误用未受信任连接。 */
    assert(create_client_context_with_stderr_suppressed(
               "certs/missing.crt", "certs/client.crt", "certs/client.key") == NULL);

    SSL_CTX_free(client_ctx);
    SSL_CTX_free(server_ctx);
    return 0;
}
