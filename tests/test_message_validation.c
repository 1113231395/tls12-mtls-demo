#include "tls_common.h"

#include <assert.h>

int main(void)
{
    /* 消息协议拒绝空载荷，接受 1 字节到最大上限之间的长度。 */
    assert(tls_validate_message_length(0U) == 0);
    assert(tls_validate_message_length(1U) == 1);
    assert(tls_validate_message_length(TLS_MAX_MESSAGE_SIZE) == 1);
    /* 超过上限的长度必须被拒绝，防止发送或接收侧处理过大帧。 */
    assert(tls_validate_message_length(TLS_MAX_MESSAGE_SIZE + 1U) == 0);
    return 0;
}
