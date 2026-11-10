#include "esp_event.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "nvs_flash.h"

#if CONFIG_HUB_H2_UART_DIAGNOSTIC_ENABLE
void hub_h2_uart_diagnostic_run(void);
#endif

static const char *TAG = "hub_h2_zigbee_rcp";

void app_main(void)
{
#if CONFIG_HUB_H2_UART_DIAGNOSTIC_ENABLE
    hub_h2_uart_diagnostic_run();
    return;
#endif

    esp_err_t status = nvs_flash_init();
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s; persistent storage was not erased",
                 esp_err_to_name(status));
        return;
    }

    status = esp_event_loop_create_default();
    if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Default event loop init failed: %s",
                 esp_err_to_name(status));
        return;
    }

    const esp_vfs_eventfd_config_t eventfd_config = {.max_fds = 2};
    status = esp_vfs_eventfd_register(&eventfd_config);
    if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "eventfd init failed: %s", esp_err_to_name(status));
        return;
    }

    const esp_openthread_config_t config = {
        .netif_config = {0},
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };
    status = esp_openthread_start(&config);
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "Zigbee RCP startup failed: %s", esp_err_to_name(status));
        return;
    }

    ESP_LOGI(TAG, "H2 radio RCP started; host owns Zigbee coordinator state");
}