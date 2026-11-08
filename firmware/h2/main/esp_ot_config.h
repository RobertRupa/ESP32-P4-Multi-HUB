#pragma once

#include "esp_openthread_types.h"

#define HUB_H2_RCP_UART_BAUD 230400

#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG() \
    { .radio_mode = RADIO_MODE_NATIVE }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG() \
    { \
        .host_connection_mode = HOST_CONNECTION_MODE_RCP_UART, \
        .host_uart_config = { \
            .port = UART_NUM_0, \
            .uart_config = { \
                .baud_rate = HUB_H2_RCP_UART_BAUD, \
                .data_bits = UART_DATA_8_BITS, \
                .parity = UART_PARITY_DISABLE, \
                .stop_bits = UART_STOP_BITS_1, \
                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, \
                .rx_flow_ctrl_thresh = 0, \
                .source_clk = UART_SCLK_DEFAULT, \
            }, \
            .rx_pin = UART_PIN_NO_CHANGE, \
            .tx_pin = UART_PIN_NO_CHANGE, \
        }, \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG() \
    { \
        .storage_partition_name = "nvs", \
        .netif_queue_size = 10, \
        .task_queue_size = 10, \
    }
