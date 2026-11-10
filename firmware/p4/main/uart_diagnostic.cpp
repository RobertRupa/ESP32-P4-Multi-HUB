#include "uart_diagnostic.hpp"

#include "uart_diagnostic_protocol.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace hub::diagnostics {
namespace {

constexpr char kTag[] = "hub_p4_uart_diag";

}

void run_uart_diagnostic() noexcept {
    constexpr uart_port_t kPort =
        static_cast<uart_port_t>(CONFIG_HUB_UART_DIAGNOSTIC_PORT);
    constexpr int kTxGpio = CONFIG_HUB_UART_DIAGNOSTIC_TX_GPIO;
    constexpr int kRxGpio = CONFIG_HUB_UART_DIAGNOSTIC_RX_GPIO;

    if (kTxGpio < 0 || kRxGpio < 0 || kTxGpio == kRxGpio) {
        ESP_LOGE(kTag, "Invalid UART diagnostic GPIO configuration");
        return;
    }

    uart_config_t config{};
    config.baud_rate = 230400;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    esp_err_t status = uart_driver_install(kPort, 256, 256, 0, nullptr, 0);
    if (status == ESP_OK) {
        status = uart_param_config(kPort, &config);
    }
    if (status == ESP_OK) {
        status = uart_set_pin(kPort, kTxGpio, kRxGpio,
                              UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "UART setup failed: %s", esp_err_to_name(status));
        return;
    }

    ESP_LOGI(kTag, "UART diagnostic ready: UART%d, TX=%d RX=%d, 230400 8N1",
             static_cast<int>(kPort), kTxGpio, kRxGpio);
    hub_uart_diag_parser_t parser{};
    uint8_t input[32];
    uint8_t frame_bytes[HUB_UART_DIAG_FRAME_SIZE];
    uint32_t pings = 0;
    uint32_t peer_acks = 0;
    uint32_t local_echoes = 0;
    uint32_t bad_crc = 0;
    uint32_t rx_bytes = 0;
    uint8_t idle_reads = 0;
    uint16_t sequence = 0;
    TickType_t last_probe = 0;
    bool probe_started = false;

    while (true) {
        const TickType_t now = xTaskGetTickCount();
        if (!probe_started || now - last_probe >= pdMS_TO_TICKS(500)) {
            hub_uart_diag_encode(HUB_UART_DIAG_TYPE_PING,
                                 HUB_UART_DIAG_SENDER_P4, sequence, frame_bytes);
            const int written = uart_write_bytes(kPort, frame_bytes,
                                                  sizeof(frame_bytes));
            if (written == sizeof(frame_bytes)) {
                ESP_LOGI(kTag, "TX PING seq=%u", static_cast<unsigned>(sequence));
                ++sequence;
                last_probe = now;
                probe_started = true;
            } else {
                ESP_LOGE(kTag, "PING TX failed seq=%u",
                         static_cast<unsigned>(sequence));
            }
        }

        const int received = uart_read_bytes(kPort, input, sizeof(input),
                                             pdMS_TO_TICKS(250));
        if (received < 0) {
            ESP_LOGE(kTag, "UART read failed: %d", received);
            continue;
        }
        if (received == 0) {
            if (++idle_reads == 8) {
                ESP_LOGW(kTag, "RX idle bytes=%lu pings=%lu crc_errors=%lu",
                         static_cast<unsigned long>(rx_bytes),
                         static_cast<unsigned long>(pings),
                         static_cast<unsigned long>(bad_crc));
                idle_reads = 0;
            }
            continue;
        }

        idle_reads = 0;
        rx_bytes += static_cast<uint32_t>(received);
        ESP_LOG_BUFFER_HEX_LEVEL(kTag, input, received, ESP_LOG_INFO);
        for (int index = 0; index < received; ++index) {
            hub_uart_diag_frame_t frame{};
            const int parsed = hub_uart_diag_parser_push(
                &parser, input[index], &frame);
            if (parsed < 0) {
                ++bad_crc;
                ESP_LOGW(kTag, "RX CRC error total=%lu",
                         static_cast<unsigned long>(bad_crc));
            } else if (parsed > 0 &&
                       frame.type == HUB_UART_DIAG_TYPE_PING &&
                       frame.sender == HUB_UART_DIAG_SENDER_P4) {
                ++local_echoes;
                ESP_LOGI(kTag, "LOCAL ECHO seq=%u total=%lu",
                         static_cast<unsigned>(frame.sequence),
                         static_cast<unsigned long>(local_echoes));
            } else if (parsed > 0 &&
                       frame.type == HUB_UART_DIAG_TYPE_PING &&
                       frame.sender == HUB_UART_DIAG_SENDER_H2) {
                hub_uart_diag_encode(HUB_UART_DIAG_TYPE_ACK,
                                     HUB_UART_DIAG_SENDER_P4, frame.sequence,
                                     frame_bytes);
                const int written = uart_write_bytes(kPort, frame_bytes,
                                                      sizeof(frame_bytes));
                if (written == sizeof(frame_bytes)) {
                    ++pings;
                    ESP_LOGI(kTag, "PEER PING seq=%u ACK sent total=%lu",
                             static_cast<unsigned>(frame.sequence),
                             static_cast<unsigned long>(pings));
                } else {
                    ESP_LOGE(kTag, "ACK TX failed for seq=%u",
                             static_cast<unsigned>(frame.sequence));
                }
            } else if (parsed > 0 &&
                       frame.type == HUB_UART_DIAG_TYPE_ACK &&
                       frame.sender == HUB_UART_DIAG_SENDER_H2) {
                ++peer_acks;
                ESP_LOGI(kTag, "PEER ACK seq=%u total=%lu",
                         static_cast<unsigned>(frame.sequence),
                         static_cast<unsigned long>(peer_acks));
            } else if (parsed > 0) {
                ESP_LOGW(kTag, "Unexpected frame type=%u seq=%u",
                         static_cast<unsigned>(frame.type),
                         static_cast<unsigned>(frame.sequence));
            }
        }
    }
}

}