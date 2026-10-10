#define _POSIX_C_SOURCE 200112L

#include "tls_common.h"

#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* 客户端命令行参数集中保存证书路径、目标地址和校验名称。 */
struct client_options {
    const char *ca_file;
    const char *cert_file;
    const char *host;
    const char *key_file;
    const char *verify_name;
    unsigned short port;
};

static void print_usage(const char *program)
{
    fprintf(stdout,
            "Usage: %s [--host HOST] [--port PORT] [--verify-name NAME] "
            "[--ca FILE] [--cert FILE] [--key FILE] MESSAGE\n",
            program);
}

static int parse_port(const char *text, unsigned short *port_out)
{
    char *end;
    long value;

    /* 端口必须完整解析为 1..65535，避免静默接受部分字符串。 */
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

static int parse_options(int argc, char *argv[], struct client_options *options,
                         const char **message_out)
{
    static const struct option long_options[] = {
        {"host", required_argument, NULL, 'H'},
        {"port", required_argument, NULL, 'p'},
        {"verify-name", required_argument, NULL, 'v'},
        {"ca", required_argument, NULL, 'a'},
        {"cert", required_argument, NULL, 'c'},
        {"key", required_argument, NULL, 'k'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int option;

    /* 默认值匹配脚本生成的 demo 证书和嵌入式设备要连接的服务端地址。 */
    options->ca_file = "certs/ca.crt";
    options->cert_file = "certs/client.crt";
    options->host = "192.168.168.142";
    options->key_file = "certs/client.key";
    options->verify_name = "192.168.168.142";
    options->port = 18443U;

    opterr = 0;
    while ((option = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (option) {
        case 'H':
            options->host = optarg;
            break;
        case 'p':
            if (!parse_port(optarg, &options->port)) {
                fprintf(stderr, "invalid port: %s\n", optarg);
                return 0;
            }
            break;
        case 'v':
            options->verify_name = optarg;
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

    if (optind + 1 != argc) {
        fprintf(stderr, "exactly one message is required\n");
        return 0;
    }

    *message_out = argv[optind];
    return 1;
}

static int ignore_sigpipe(void)
{
    struct sigaction action;

    /* TLS 写入失败应通过返回值处理，而不是让 SIGPIPE 终止进程。 */
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_IGN;
    if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGPIPE, &action, NULL) != 0) {
        fprintf(stderr, "ignore SIGPIPE: %s\n", strerror(errno));
        return 0;
    }
    return 1;
}

static int connect_to_host(const char *host, unsigned short port)
{
    struct addrinfo *addresses;
    struct addrinfo *address;
    struct addrinfo hints;
    char port_text[6];
    int connect_error;
    int socket_fd;
    int status;

    if (snprintf(port_text, sizeof(port_text), "%u", (unsigned int)port) < 0) {
        fprintf(stderr, "format port: %s\n", strerror(errno));
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    status = getaddrinfo(host, port_text, &hints, &addresses);
    if (status != 0) {
        fprintf(stderr, "resolve host: %s\n", gai_strerror(status));
        return -1;
    }

    socket_fd = -1;
    connect_error = 0;
    /* 逐个尝试解析出的 IPv4/IPv6 地址，直到某个地址连接成功。 */
    for (address = addresses; address != NULL; address = address->ai_next) {
        socket_fd = socket(address->ai_family, address->ai_socktype,
                           address->ai_protocol);
        if (socket_fd < 0) {
            connect_error = errno;
            continue;
        }
        if (connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        connect_error = errno;
        close(socket_fd);
        socket_fd = -1;
    }
    freeaddrinfo(addresses);

    if (socket_fd < 0) {
        if (connect_error == 0) {
            connect_error = ECONNREFUSED;
        }
        fprintf(stderr, "connect to server: %s\n", strerror(connect_error));
    }
    return socket_fd;
}

int main(int argc, char *argv[])
{
    unsigned char echo[TLS_MAX_MESSAGE_SIZE + 1U];
    struct client_options options;
    const char *message;
    size_t message_size;
    uint32_t echo_length;
    uint32_t message_length;
    SSL_CTX *context;
    SSL *ssl;
    int parse_result;
    int socket_fd;
    int status;

    parse_result = parse_options(argc, argv, &options, &message);
    if (parse_result < 0) {
        return EXIT_SUCCESS;
    }
    if (parse_result == 0) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    /* 先检查消息长度，避免无效输入还建立 TCP/TLS 连接。 */
    message_size = strlen(message);
    if (message_size > UINT32_MAX ||
        !tls_validate_message_length((uint32_t)message_size)) {
        fprintf(stderr, "invalid message length\n");
        return EXIT_FAILURE;
    }
    message_length = (uint32_t)message_size;
    if (!ignore_sigpipe()) {
        return EXIT_FAILURE;
    }

    socket_fd = connect_to_host(options.host, options.port);
    if (socket_fd < 0) {
        return EXIT_FAILURE;
    }
    context = tls_create_client_context(options.ca_file, options.cert_file,
                                        options.key_file);
    if (context == NULL) {
        close(socket_fd);
        return EXIT_FAILURE;
    }
    ssl = SSL_new(context);
    if (ssl == NULL) {
        tls_print_errors("create client TLS connection");
        SSL_CTX_free(context);
        close(socket_fd);
        return EXIT_FAILURE;
    }
    /* verify-name 同时用于 SNI 扩展和证书主机名校验。 */
    if (!tls_configure_hostname(ssl, options.verify_name)) {
        SSL_free(ssl);
        SSL_CTX_free(context);
        close(socket_fd);
        return EXIT_FAILURE;
    }
    if (SSL_set_fd(ssl, socket_fd) != 1) {
        tls_print_errors("attach client TLS connection");
        SSL_free(ssl);
        SSL_CTX_free(context);
        close(socket_fd);
        return EXIT_FAILURE;
    }
    if (SSL_connect(ssl) != 1) {
        tls_print_errors("connect TLS handshake");
        SSL_free(ssl);
        SSL_CTX_free(context);
        close(socket_fd);
        return EXIT_FAILURE;
    }
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        fprintf(stderr, "server certificate verification failed: %ld\n",
                SSL_get_verify_result(ssl));
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(context);
        close(socket_fd);
        return EXIT_FAILURE;
    }
    if (!tls_verify_peer_hostname(ssl, options.verify_name)) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(context);
        close(socket_fd);
        return EXIT_FAILURE;
    }

    status = EXIT_FAILURE;
    tls_print_session_info(ssl, stdout);
    if (!tls_send_message(ssl, (const unsigned char *)message, message_length)) {
        tls_print_errors("send TLS message");
    } else if (!tls_receive_message(ssl, echo, TLS_MAX_MESSAGE_SIZE, &echo_length)) {
        tls_print_errors("receive TLS message");
    } else {
        /* 协议按字节传输；这里追加终止符只用于按字符串显示回显内容。 */
        echo[echo_length] = '\0';
        printf("Echo: %s\n", echo);
        status = EXIT_SUCCESS;
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(context);
    close(socket_fd);
    return status;
}
