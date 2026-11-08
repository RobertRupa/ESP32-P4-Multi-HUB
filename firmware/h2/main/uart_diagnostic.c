#include "uart_diagnostic_protocol.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hub_h2_uart_diag";

static esp_err_t configure_uart(void) {
    const uart_config_t config = {
        .baud_rate = 230400,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t status = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    if (status != ESP_OK) {
        return status;
    }
    status = uart_param_config(UART_NUM_0, &config);
    if (status != ESP_OK) {
        return status;
    }
    return uart_set_pin(UART_NUM_0, GPIO_NUM_24, GPIO_NUM_23,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void hub_h2_uart_diagnostic_run(void) {
    esp_err_t status = configure_uart();
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "UART setup failed: %s", esp_err_to_name(status));
        return;
    }

    ESP_LOGI(TAG, "UART diagnostic ready: UART0 TX=GPIO24 RX=GPIO23, 230400 8N1");
    hub_uart_diag_parser_t parser = {0};
    uint16_t sequence = 0;
    uint32_t local_echoes = 0;
    uint32_t peer_acks = 0;
    uint32_t peer_pings = 0;
    uint32_t timed_out = 0;
    uint8_t frame_bytes[HUB_UART_DIAG_FRAME_SIZE];
    uint8_t input[32];

    while (true) {
        hub_uart_diag_encode(HUB_UART_DIAG_TYPE_PING,
                     HUB_UART_DIAG_SENDER_H2, sequence, frame_bytes);
        const int written = uart_write_bytes(UART_NUM_0, frame_bytes,
                                             sizeof(frame_bytes));
        if (written != sizeof(frame_bytes)) {
            ESP_LOGE(TAG, "TX failed for seq=%u", sequence);
        } else {
            bool got_ack = false;
            for (int elapsed_ms = 0; elapsed_ms < 250 && !got_ack;
                 elapsed_ms += 25) {
                const int received = uart_read_bytes(
                    UART_NUM_0, input, sizeof(input), pdMS_TO_TICKS(25));
                for (int index = 0; index < received; ++index) {
                    hub_uart_diag_frame_t received_frame = {0};
                    const int parsed = hub_uart_diag_parser_push(
                        &parser, input[index], &received_frame);
                    if (parsed < 0) {
                        ESP_LOGW(TAG, "RX frame CRC error");
                          } else if (parsed > 0 &&
                               received_frame.type == HUB_UART_DIAG_TYPE_PING &&
                               received_frame.sender == HUB_UART_DIAG_SENDER_H2) {
                           ++local_echoes;
                           ESP_LOGI(TAG, "LOCAL ECHO seq=%u total=%lu",
                                 (unsigned)received_frame.sequence,
                                 (unsigned long)local_echoes);
                          } else if (parsed > 0 &&
                               received_frame.type == HUB_UART_DIAG_TYPE_PING &&
                               received_frame.sender == HUB_UART_DIAG_SENDER_P4) {
                           hub_uart_diag_encode(HUB_UART_DIAG_TYPE_ACK,
                                       HUB_UART_DIAG_SENDER_H2,
                                       received_frame.sequence, frame_bytes);
                           if (uart_write_bytes(UART_NUM_0, frame_bytes,
                                       sizeof(frame_bytes)) == sizeof(frame_bytes)) {
                            ++peer_pings;
                            ESP_LOGI(TAG, "PEER PING seq=%u ACK sent total=%lu",
                                  (unsigned)received_frame.sequence,
                                  (unsigned long)peer_pings);
                           }
                          } else if (parsed > 0 &&
                               received_frame.type == HUB_UART_DIAG_TYPE_ACK &&
                               received_frame.sender == HUB_UART_DIAG_SENDER_P4 &&
                               received_frame.sequence == sequence) {
                           got_ack = true;
                           ++peer_acks;
                           ESP_LOGI(TAG, "PEER ACK seq=%u total=%lu",
                                 (unsigned)sequence, (unsigned long)peer_acks);
                    }
                }
            }
            if (!got_ack) {
                ++timed_out;
                ESP_LOGW(TAG, "ACK timeout seq=%u total=%lu",
                         (unsigned)sequence, (unsigned long)timed_out);
            }
        }
        ++sequence;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}