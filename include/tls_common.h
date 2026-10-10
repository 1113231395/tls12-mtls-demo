#ifndef TLS_COMMON_H
#define TLS_COMMON_H

#include <openssl/ssl.h>

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

/* 单帧消息的载荷上限；帧头固定使用 4 字节网络序长度。 */
#define TLS_MAX_MESSAGE_SIZE 4096U

/* 统一约束业务消息长度，拒绝空消息和超过协议上限的载荷。 */
int tls_validate_message_length(uint32_t length);
/* SSL 读写可能只处理部分数据，这两个接口负责循环直到指定长度完成。 */
int tls_write_all(SSL *ssl, const void *buffer, size_t length);
int tls_read_all(SSL *ssl, void *buffer, size_t length);
/* 按“长度前缀 + 载荷”的帧格式收发一条完整消息。 */
int tls_send_message(SSL *ssl, const unsigned char *message, uint32_t length);
int tls_receive_message(SSL *ssl, unsigned char *buffer, size_t capacity,
                        uint32_t *length_out);
/* 服务端和客户端上下文都限制为 TLS 1.2，并加载 CA、证书和私钥。 */
SSL_CTX *tls_create_server_context(const char *ca_file,
                                   const char *cert_file,
                                   const char *key_file);
SSL_CTX *tls_create_client_context(const char *ca_file,
                                   const char *cert_file,
                                   const char *key_file);
/* 配置 SNI，并在握手后按主机名或 IP 校验证书身份。 */
int tls_configure_hostname(SSL *ssl, const char *hostname);
int tls_verify_peer_hostname(SSL *ssl, const char *hostname);
/* 诊断辅助函数用于输出 OpenSSL 错误栈和协商后的会话信息。 */
void tls_print_errors(const char *operation);
void tls_print_session_info(const SSL *ssl, FILE *stream);

#endif
