#ifndef HUB_UART_DIAGNOSTIC_PROTOCOL_H
#define HUB_UART_DIAGNOSTIC_PROTOCOL_H

#include <stdint.h>

#define HUB_UART_DIAG_FRAME_SIZE 6
#define HUB_UART_DIAG_SYNC 0xA5
#define HUB_UART_DIAG_TYPE_PING 0x01
#define HUB_UART_DIAG_TYPE_ACK 0x02
#define HUB_UART_DIAG_SENDER_H2 0x01
#define HUB_UART_DIAG_SENDER_P4 0x02

typedef struct {
    uint8_t bytes[HUB_UART_DIAG_FRAME_SIZE];
    uint8_t used;
} hub_uart_diag_parser_t;

typedef struct {
    uint8_t type;
    uint8_t sender;
    uint16_t sequence;
} hub_uart_diag_frame_t;

static inline uint8_t hub_uart_diag_crc8(const uint8_t *data, uint8_t length) {
    uint8_t crc = 0;
    for (uint8_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07)
                               : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static inline void hub_uart_diag_encode(uint8_t type, uint8_t sender,
                                        uint16_t sequence, uint8_t *output) {
    output[0] = HUB_UART_DIAG_SYNC;
    output[1] = type;
    output[2] = sender;
    output[3] = (uint8_t)(sequence >> 8);
    output[4] = (uint8_t)sequence;
    output[5] = hub_uart_diag_crc8(&output[1], 4);
}

static inline int hub_uart_diag_parser_push(hub_uart_diag_parser_t *parser,
                                            uint8_t byte,
                                            hub_uart_diag_frame_t *frame) {
    if (parser->used == 0) {
        if (byte != HUB_UART_DIAG_SYNC) {
            return 0;
        }
        parser->bytes[parser->used++] = byte;
        return 0;
    }

    parser->bytes[parser->used++] = byte;
    if (parser->used < HUB_UART_DIAG_FRAME_SIZE) {
        return 0;
    }
    parser->used = 0;

    if (hub_uart_diag_crc8(&parser->bytes[1], 4) != parser->bytes[5]) {
        return -1;
    }

    frame->type = parser->bytes[1];
    frame->sender = parser->bytes[2];
    frame->sequence = (uint16_t)(((uint16_t)parser->bytes[3] << 8) |
                                 parser->bytes[4]);
    return 1;
}

#endif