#include "tls_common.h"

#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/* 子进程充当本地 TLS 回显服务端，复用生产代码的收发接口。 */
static void run_server(SSL_CTX *context, int socket_fd)
{
    SSL *ssl;
    unsigned char message[TLS_MAX_MESSAGE_SIZE];
    uint32_t length;

    ssl = SSL_new(context);
    assert(ssl != NULL);
    assert(SSL_set_fd(ssl, socket_fd) == 1);
    assert(SSL_accept(ssl) == 1);
    assert(tls_receive_message(ssl, message, sizeof(message), &length) == 1);
    assert(length == sizeof("hello tls") - 1U);
    assert(tls_send_message(ssl, message, length) == 1);
    (void)SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(context);
    assert(close(socket_fd) == 0);
}

int main(void)
{
    SSL_CTX *client_context;
    SSL_CTX *server_context;
    SSL *client_ssl;
    unsigned char response[TLS_MAX_MESSAGE_SIZE];
    static const unsigned char message[] = "hello tls";
    static const unsigned char oversized[] = "x";
    int sockets[2];
    int status;
    pid_t child;
    uint32_t response_length;

    assert(SSL_library_init() == 1);
    /* 使用本地套接字对和子进程模拟 TLS 双端，避免占用真实 TCP 端口。 */
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    server_context = tls_create_server_context("certs/ca.crt", "certs/server.crt",
                                               "certs/server.key");
    client_context = tls_create_client_context("certs/ca.crt", "certs/client.crt",
                                               "certs/client.key");
    assert(server_context != NULL);
    assert(client_context != NULL);

    /* fork 后子进程只保留服务端套接字，父进程只保留客户端套接字。 */
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        assert(close(sockets[0]) == 0);
        run_server(server_context, sockets[1]);
        _exit(0);
    }

    assert(close(sockets[1]) == 0);
    SSL_CTX_free(server_context);
    client_ssl = SSL_new(client_context);
    assert(client_ssl != NULL);
    assert(tls_configure_hostname(client_ssl, "localhost") == 1);
    assert(SSL_set_fd(client_ssl, sockets[0]) == 1);
    assert(SSL_connect(client_ssl) == 1);

    /* 客户端发送一帧消息并校验服务端按原字节回显。 */
    assert(tls_send_message(client_ssl, message, sizeof(message) - 1U) == 1);
    assert(tls_receive_message(client_ssl, response, sizeof(response),
                               &response_length) == 1);
    assert(response_length == sizeof(message) - 1U);
    assert(memcmp(response, message, response_length) == 0);

    (void)SSL_shutdown(client_ssl);
    /* 空消息和超过协议上限的消息应在发送前被拒绝。 */
    assert(tls_send_message(client_ssl, (const unsigned char *)"", 0U) == 0);
    assert(tls_send_message(client_ssl, oversized, TLS_MAX_MESSAGE_SIZE + 1U) == 0);

    SSL_free(client_ssl);
    SSL_CTX_free(client_context);
    assert(close(sockets[0]) == 0);
    /* 子进程必须正常退出，证明服务端路径完整处理了握手、收包和回显。 */
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    return 0;
}
