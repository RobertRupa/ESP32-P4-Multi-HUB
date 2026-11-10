#include "uart_diagnostic_protocol.h"

#include <cassert>
#include <cstdint>

static int parse_frame(hub_uart_diag_parser_t* parser, const uint8_t* bytes,
                       hub_uart_diag_frame_t* frame) {
    int result = 0;
    for (uint8_t index = 0; index < HUB_UART_DIAG_FRAME_SIZE; ++index) {
        result = hub_uart_diag_parser_push(parser, bytes[index], frame);
    }
    return result;
}

int main() {
    hub_uart_diag_parser_t parser{};
    hub_uart_diag_frame_t decoded{};
    uint8_t encoded[HUB_UART_DIAG_FRAME_SIZE]{};

    hub_uart_diag_encode(HUB_UART_DIAG_TYPE_PING, HUB_UART_DIAG_SENDER_H2,
                         0x1234, encoded);
    assert(parse_frame(&parser, encoded, &decoded) == 1);
    assert(decoded.type == HUB_UART_DIAG_TYPE_PING);
    assert(decoded.sender == HUB_UART_DIAG_SENDER_H2);
    assert(decoded.sequence == 0x1234);

    hub_uart_diag_encode(HUB_UART_DIAG_TYPE_PING, HUB_UART_DIAG_SENDER_P4,
                         0x1234, encoded);
    assert(parse_frame(&parser, encoded, &decoded) == 1);
    assert(decoded.type == HUB_UART_DIAG_TYPE_PING);
    assert(decoded.sender == HUB_UART_DIAG_SENDER_P4);
    assert(decoded.sequence == 0x1234);

    hub_uart_diag_encode(HUB_UART_DIAG_TYPE_ACK, HUB_UART_DIAG_SENDER_P4,
                         0x0000, encoded);
    assert(parse_frame(&parser, encoded, &decoded) == 1);
    assert(decoded.type == HUB_UART_DIAG_TYPE_ACK);
    assert(decoded.sender == HUB_UART_DIAG_SENDER_P4);
    assert(decoded.sequence == 0);

    encoded[2] ^= 0x01;
    assert(parse_frame(&parser, encoded, &decoded) == -1);

    return 0;
}