#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_HUB_C6_ZIGBEE_COORD_ENABLE
#include "c6_zigbee_sdio.h"
#endif

#if CONFIG_HUB_MULTIPAN_RCP_ENABLE
#include "driver/uart.h"
#if !CONFIG_HUB_C6_ZIGBEE_COORD_ENABLE
#include "esp_coexist.h"
#endif
#include "esp_openthread.h"
#include "esp_openthread_types.h"
#include "esp_vfs_eventfd.h"
#endif

static const char *TAG = "hub_c6_cp";

#if CONFIG_HUB_MULTIPAN_RCP_ENABLE
static esp_err_t start_multipan_rcp(void)
{
    const esp_vfs_eventfd_config_t eventfd_config = {.max_fds = 2};
    esp_err_t result = esp_vfs_eventfd_register(&eventfd_config);
    if (result != ESP_OK) {
        return result;
    }

    static const esp_openthread_config_t rcp_config = {
        .netif_config = {0},
        .platform_config = {
            .radio_config = {
                .radio_mode = RADIO_MODE_NATIVE,
            },
            .host_config = {
                .host_connection_mode = HOST_CONNECTION_MODE_RCP_UART,
                .host_uart_config = {
                    .port = UART_NUM_0,
                    .uart_config = {
                        .baud_rate = 460800,
                        .data_bits = UART_DATA_8_BITS,
                        .parity = UART_PARITY_DISABLE,
                        .stop_bits = UART_STOP_BITS_1,
                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                        .rx_flow_ctrl_thresh = 0,
                        .source_clk = UART_SCLK_DEFAULT,
                    },
                    .rx_pin = UART_PIN_NO_CHANGE,
                    .tx_pin = UART_PIN_NO_CHANGE,
                },
            },
            .port_config = {
                .storage_partition_name = "nvs",
                .netif_queue_size = 10,
                .task_queue_size = 10,
            },
        },
    };

    result = esp_openthread_start(&rcp_config);
    if (result != ESP_OK) {
        return result;
    }
    return esp_coex_wifi_i154_enable();
}
#endif

void app_main(void)
{
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s; refusing to erase persistent storage",
                 esp_err_to_name(result));
        return;
    }

    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Default event loop init failed: %s", esp_err_to_name(result));
        return;
    }

#if CONFIG_HUB_MULTIPAN_RCP_ENABLE
    result = start_multipan_rcp();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Multipan RCP startup failed: %s", esp_err_to_name(result));
        return;
    }
#endif

#if CONFIG_HUB_C6_ZIGBEE_COORD_ENABLE
    result = c6_zigbee_sdio_start();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "C6 Zigbee SDIO bridge failed to start: %s",
                 esp_err_to_name(result));
        return;
    }
#endif

    ESP_LOGI(TAG, "ESP-Hosted C6 co-processor application started");
}