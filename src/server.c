#define _POSIX_C_SOURCE 200112L

#include "tls_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* 信号处理函数只设置这个标志，主循环负责安全释放资源后退出。 */
static volatile sig_atomic_t stop_requested;

/* 服务端命令行参数保存监听端口以及 mTLS 所需的证书文件。 */
struct server_options {
    const char *ca_file;
    const char *cert_file;
    const char *key_file;
    unsigned short port;
};

static void print_usage(const char *program)
{
    fprintf(stdout,
            "Usage: %s [--port PORT] [--ca FILE] [--cert FILE] [--key FILE]\n",
            program);
}

static int parse_port(const char *text, unsigned short *port_out)
{
    char *end;
    long value;

    /* 端口必须完整解析为 1..65535，避免无效监听地址进入后续流程。 */
    errno = 0;
    end = NULL;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1L ||
        value > 65535L) {
        return 0;
    }

    *port_out = (unsigned short)value;
    return 1;
}

static int parse_options(int argc, char *argv[], struct server_options *options)
{
    static const struct option long_options[] = {
        {"port", required_argument, NULL, 'p'},
        {"ca", required_argument, NULL, 'a'},
        {"cert", required_argument, NULL, 'c'},
        {"key", required_argument, NULL, 'k'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int option;

    /* 默认值对应 generate_certs.sh 生成的服务端证书和本地测试端口。 */
    options->ca_file = "certs/ca.crt";
    options->cert_file = "certs/server.crt";
    options->key_file = "certs/server.key";
    options->port = 18443U;

    opterr = 0;
    while ((option = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (option) {
        case 'p':
            if (!parse_port(optarg, &options->port)) {
                fprintf(stderr, "invalid port: %s\n", optarg);
                return 0;
            }
            break;
        case 'a':
            options->ca_file = optarg;
            break;
        case 'c':
            options->cert_file = optarg;
            break;
        case 'k':
            options->key_file = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return -1;
        default:
            fprintf(stderr, "unknown option\n");
            return 0;
        }
    }

    if (optind != argc) {
        fprintf(stderr, "unexpected argument: %s\n", argv[optind]);
        return 0;
    }

    return 1;
}

/* 信号处理函数只记录退出请求，资源释放留给主循环统一处理。 */
static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction action;

    /* SIGINT 用于优雅停止服务端，避免直接中断在 TLS 或 socket 清理中。 */
    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGINT, &action, NULL) != 0) {
        fprintf(stderr, "install SIGINT handler: %s\n", strerror(errno));
        return 0;
    }

    /* 客户端提前断开时，SIGPIPE 不应终止服务端进程。 */
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_IGN;
    if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGPIPE, &action, NULL) != 0) {
        fprintf(stderr, "ignore SIGPIPE: %s\n", strerror(errno));
        return 0;
    }

    return 1;
}

static int create_listening_socket(unsigned short port)
{
    struct sockaddr_in address;
    int enabled;
    int socket_fd;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        fprintf(stderr, "create listening socket: %s\n", strerror(errno));
        return -1;
    }

    /* 测试反复启动服务端时允许快速复用刚释放的本地端口。 */
    enabled = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enabled,
                   sizeof(enabled)) != 0) {
        fprintf(stderr, "set SO_REUSEADDR: %s\n", strerror(errno));
        close(socket_fd);
        return -1;
    }

    /* demo 服务端绑定所有 IPv4 地址，端口由命令行或默认值决定。 */
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(socket_fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        fprintf(stderr, "bind listening socket: %s\n", strerror(errno));
        close(socket_fd);
        return -1;
    }
    if (listen(socket_fd, 16) != 0) {
        fprintf(stderr, "listen: %s\n", strerror(errno));
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

/* 每个连接只处理一条消息；单个连接失败不会影响服务端接受下一个连接。 */
static void serve_connection(SSL_CTX *context, int client_fd)
{
    unsigned char message[TLS_MAX_MESSAGE_SIZE + 1U];
    uint32_t message_length;
    SSL *ssl;

    ssl = SSL_new(context);
    if (ssl == NULL) {
        tls_print_errors("create server TLS connection");
        close(client_fd);
        return;
    }

    if (SSL_set_fd(ssl, client_fd) != 1) {
        tls_print_errors("attach server TLS connection");
        SSL_free(ssl);
        close(client_fd);
        return;
    }
    if (SSL_accept(ssl) != 1) {
        tls_print_errors("accept TLS handshake");
        SSL_free(ssl);
        close(client_fd);
        return;
    }
    /* 服务端要求客户端证书，握手后再次检查校验结果以便输出明确错误。 */
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        fprintf(stderr, "client certificate verification failed: %ld\n",
                SSL_get_verify_result(ssl));
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client_fd);
        return;
    }

    tls_print_session_info(ssl, stdout);
    if (!tls_receive_message(ssl, message, TLS_MAX_MESSAGE_SIZE, &message_length)) {
        tls_print_errors("receive TLS message");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client_fd);
        return;
    }

    /* 追加的结尾字节只用于日志打印，不属于回显载荷。 */
    message[message_length] = '\0';
    printf("Received: %s\n", message);
    fflush(stdout);
    if (!tls_send_message(ssl, message, message_length)) {
        tls_print_errors("send TLS message");
    }
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client_fd);
}

int main(int argc, char *argv[])
{
    struct server_options options;
    SSL_CTX *context;
    int listen_fd;
    int parse_result;
    int status;

    parse_result = parse_options(argc, argv, &options);
    if (parse_result < 0) {
        return EXIT_SUCCESS;
    }
    if (parse_result == 0) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (!install_signal_handlers()) {
        return EXIT_FAILURE;
    }

    context = tls_create_server_context(options.ca_file, options.cert_file,
                                        options.key_file);
    if (context == NULL) {
        return EXIT_FAILURE;
    }
    listen_fd = create_listening_socket(options.port);
    if (listen_fd < 0) {
        SSL_CTX_free(context);
        return EXIT_FAILURE;
    }

    printf("Listening on port %u\n", (unsigned int)options.port);
    fflush(stdout);
    status = EXIT_SUCCESS;
    while (!stop_requested) {
        int client_fd;

        client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            /* SIGINT 会让 accept 返回 EINTR，随后循环条件负责退出。 */
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "accept connection: %s\n", strerror(errno));
            status = EXIT_FAILURE;
            break;
        }
        serve_connection(context, client_fd);
    }

    close(listen_fd);
    SSL_CTX_free(context);
    return status;
}
