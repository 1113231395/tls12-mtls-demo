#include "tls_common.h"

#include <arpa/inet.h>
#include <limits.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <string.h>

int tls_write_all(SSL *ssl, const void *buffer, size_t length)
{
    const unsigned char *bytes;
    size_t offset;

    if (ssl == NULL || (buffer == NULL && length != 0U)) {
        return 0;
    }

    bytes = buffer;
    offset = 0U;
    while (offset < length) {
        size_t remaining;
        size_t chunk_size;
        int error;
        int written;

        /* SSL_write 的长度参数是 int，超出范围时需要分段发送。 */
        remaining = length - offset;
        chunk_size = remaining > (size_t)INT_MAX ? (size_t)INT_MAX : remaining;
        written = SSL_write(ssl, bytes + offset, (int)chunk_size);
        if (written > 0) {
            offset += (size_t)written;
            continue;
        }

        /* 非阻塞式重试信号由 OpenSSL 返回，其他错误交给调用方处理。 */
        error = SSL_get_error(ssl, written);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
            return 0;
        }
    }

    return 1;
}

int tls_read_all(SSL *ssl, void *buffer, size_t length)
{
    unsigned char *bytes;
    size_t offset;

    if (ssl == NULL || (buffer == NULL && length != 0U)) {
        return 0;
    }

    bytes = buffer;
    offset = 0U;
    while (offset < length) {
        size_t remaining;
        size_t chunk_size;
        int error;
        int read_count;

        /* 与发送侧保持相同的分段规则，直到读满调用方要求的长度。 */
        remaining = length - offset;
        chunk_size = remaining > (size_t)INT_MAX ? (size_t)INT_MAX : remaining;
        read_count = SSL_read(ssl, bytes + offset, (int)chunk_size);
        if (read_count > 0) {
            offset += (size_t)read_count;
            continue;
        }

        /* WANT_READ/WANT_WRITE 表示握手或 I/O 状态需要继续驱动。 */
        error = SSL_get_error(ssl, read_count);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
            return 0;
        }
    }

    return 1;
}

int tls_send_message(SSL *ssl, const unsigned char *message, uint32_t length)
{
    uint32_t network_length;

    if (!tls_validate_message_length(length) || message == NULL) {
        return 0;
    }

    /* 先写网络序长度，再写载荷，让接收端能恢复明确的消息边界。 */
    network_length = htonl(length);
    return tls_write_all(ssl, &network_length, sizeof(network_length)) &&
           tls_write_all(ssl, message, length);
}

int tls_receive_message(SSL *ssl, unsigned char *buffer, size_t capacity,
                        uint32_t *length_out)
{
    uint32_t network_length;
    uint32_t length;

    if (buffer == NULL || length_out == NULL ||
        !tls_read_all(ssl, &network_length, sizeof(network_length))) {
        return 0;
    }

    /* 读取载荷前先校验长度，避免越界写入或等待不受支持的大消息。 */
    length = ntohl(network_length);
    if (!tls_validate_message_length(length) || (size_t)length > capacity) {
        return 0;
    }

    if (!tls_read_all(ssl, buffer, (size_t)length)) {
        return 0;
    }

    *length_out = length;
    return 1;
}

int tls_validate_message_length(uint32_t length)
{
    return length > 0U && length <= TLS_MAX_MESSAGE_SIZE;
}

/* 服务端和客户端复用同一套 TLS 上下文配置，只通过校验模式区分角色。 */
static SSL_CTX *configure_context(const SSL_METHOD *method, const char *ca_file,
                                  const char *cert_file, const char *key_file,
                                  int verify_mode)
{
    SSL_CTX *ctx;

    ctx = SSL_CTX_new(method);
    if (ctx == NULL) {
        tls_print_errors("create TLS context");
        return NULL;
    }

    SSL_CTX_set_verify(ctx, verify_mode, NULL);
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    /* OpenSSL 1.0.x 没有 min/max proto API，直接选择 TLS 1.2 方法。 */
#else
    if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION) != 1) {
        tls_print_errors("configure TLS version");
        SSL_CTX_free(ctx);
        return NULL;
    }
#endif
    /* 要求 CA、证书和私钥全部配置成功。 */
    if (SSL_CTX_load_verify_locations(ctx, ca_file, NULL) != 1 ||
        SSL_CTX_use_certificate_chain_file(ctx, cert_file) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(ctx) != 1) {
        tls_print_errors("configure TLS context");
        SSL_CTX_free(ctx);
        return NULL;
    }

    return ctx;
}

SSL_CTX *tls_create_server_context(const char *ca_file, const char *cert_file,
                                   const char *key_file)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    return configure_context(TLSv1_2_server_method(), ca_file, cert_file, key_file,
                             SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT);
#else
    return configure_context(TLS_server_method(), ca_file, cert_file, key_file,
                             SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT);
#endif
}

SSL_CTX *tls_create_client_context(const char *ca_file, const char *cert_file,
                                   const char *key_file)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    return configure_context(TLSv1_2_client_method(), ca_file, cert_file, key_file,
                             SSL_VERIFY_PEER);
#else
    return configure_context(TLS_client_method(), ca_file, cert_file, key_file,
                             SSL_VERIFY_PEER);
#endif
}

int tls_configure_hostname(SSL *ssl, const char *hostname)
{
    /* SNI 影响服务端证书选择；OpenSSL 1.0.1 需在握手后手动校验证书身份。 */
    if (SSL_set_tlsext_host_name(ssl, hostname) != 1) {
        tls_print_errors("configure TLS hostname");
        return 0;
    }

    return 1;
}

#if OPENSSL_VERSION_NUMBER >= 0x10002000L || defined(TLS_HAVE_X509_CHECK_HOST)
#if OPENSSL_VERSION_NUMBER < 0x10002000L
int X509_check_host(X509 *x, const char *chk, size_t chklen,
                    unsigned int flags, char **peername);
int X509_check_ip_asc(X509 *x, const char *ipasc, unsigned int flags);
#endif

static int verify_ip_address(X509 *cert, const char *hostname)
{
    return X509_check_ip_asc(cert, hostname, 0) == 1;
}

static int verify_dns_name(X509 *cert, const char *hostname)
{
    return X509_check_host(cert, hostname, 0U, 0U, NULL) == 1;
}
#else
static const unsigned char *tls_asn1_string_data(const ASN1_STRING *string)
{
    return ASN1_STRING_data((ASN1_STRING *)string);
}

static int ascii_equal(const unsigned char *left, int left_length, const char *right)
{
    size_t right_length;

    if (left == NULL || right == NULL || left_length < 0) {
        return 0;
    }
    right_length = strlen(right);
    return right_length == (size_t)left_length &&
           memcmp(left, right, (size_t)left_length) == 0;
}

static int verify_ip_address(X509 *cert, const char *hostname)
{
    GENERAL_NAMES *names;
    unsigned char ip_bytes[sizeof(struct in6_addr)];
    int ip_length;
    int index;
    int matched;

    if (inet_pton(AF_INET, hostname, ip_bytes) == 1) {
        ip_length = 4;
    } else if (inet_pton(AF_INET6, hostname, ip_bytes) == 1) {
        ip_length = 16;
    } else {
        return 0;
    }

    matched = 0;
    names = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
    if (names == NULL) {
        return 0;
    }

    for (index = 0; index < sk_GENERAL_NAME_num(names); ++index) {
        const GENERAL_NAME *name;
        const ASN1_OCTET_STRING *ip;

        name = sk_GENERAL_NAME_value(names, index);
        if (name->type != GEN_IPADD) {
            continue;
        }
        ip = name->d.iPAddress;
        if (ASN1_STRING_length(ip) == ip_length &&
            memcmp(tls_asn1_string_data(ip), ip_bytes, (size_t)ip_length) == 0) {
            matched = 1;
            break;
        }
    }

    GENERAL_NAMES_free(names);
    return matched;
}

static int verify_dns_name(X509 *cert, const char *hostname)
{
    GENERAL_NAMES *names;
    int index;
    int matched;
    int saw_dns_name;

    matched = 0;
    saw_dns_name = 0;
    names = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
    if (names != NULL) {
        for (index = 0; index < sk_GENERAL_NAME_num(names); ++index) {
            const GENERAL_NAME *name;
            const ASN1_IA5STRING *dns;

            name = sk_GENERAL_NAME_value(names, index);
            if (name->type != GEN_DNS) {
                continue;
            }
            saw_dns_name = 1;
            dns = name->d.dNSName;
            if (ascii_equal(tls_asn1_string_data(dns), ASN1_STRING_length(dns), hostname)) {
                matched = 1;
                break;
            }
        }
        GENERAL_NAMES_free(names);
    }

    if (matched || saw_dns_name) {
        return matched;
    }

    /* 旧证书可能没有 SAN，兼容性地回退到 CN 精确匹配。 */
    {
        char common_name[256];
        int length;

        length = X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName,
                                           common_name, sizeof(common_name));
        return length >= 0 && ascii_equal((const unsigned char *)common_name, length,
                                          hostname);
    }
}
#endif

int tls_verify_peer_hostname(SSL *ssl, const char *hostname)
{
    X509 *peer;
    int matched;

    peer = SSL_get_peer_certificate(ssl);
    if (peer == NULL) {
        fprintf(stderr, "peer certificate is missing\n");
        return 0;
    }

    matched = verify_ip_address(peer, hostname) || verify_dns_name(peer, hostname);
    X509_free(peer);
    if (!matched) {
        fprintf(stderr, "peer certificate name does not match: %s\n", hostname);
        return 0;
    }
    return 1;
}

void tls_print_errors(const char *operation)
{
    fprintf(stderr, "%s:\n", operation);
    ERR_print_errors_fp(stderr);
}

void tls_print_session_info(const SSL *ssl, FILE *stream)
{
    X509 *peer;
    char *subject;

    fprintf(stream, "Protocol: %s\n", SSL_get_version(ssl));
    fprintf(stream, "Cipher: %s\n", SSL_get_cipher(ssl));

    /* 不同 OpenSSL 版本获取对端证书的 API 名称不同，但都需要释放引用。 */
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    peer = SSL_get1_peer_certificate(ssl);
#else
    peer = SSL_get_peer_certificate(ssl);
#endif
    if (peer == NULL) {
        fprintf(stream, "Peer subject: (none)\n");
        return;
    }

    subject = X509_NAME_oneline(X509_get_subject_name(peer), NULL, 0);
    if (subject != NULL) {
        fprintf(stream, "Peer subject: %s\n", subject);
        OPENSSL_free(subject);
    } else {
        fprintf(stream, "Peer subject: (unavailable)\n");
    }
    X509_free(peer);
}
